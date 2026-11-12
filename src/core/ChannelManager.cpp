#include "ChannelManager.h"

#include "AppSettings.h"
#include "DiskMonitor.h"
#include "util/FileNameTemplate.h"
#include "Lang.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

ChannelManager::ChannelManager(QObject* parent) : QObject(parent) {
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        for (Channel* c : m_channels) {
            c->pollSource();
            c->pollRecording();
        }
        checkDisks();
        emit statsChanged();
    });
    m_poll.start();
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(2000);
    connect(&m_autosave, &QTimer::timeout, this, &ChannelManager::writeAutosave);
    connect(this, &ChannelManager::listChanged, this, &ChannelManager::markDirty);    // dodanie, usuniecie, kolejnosc
}

QList<Channel*> ChannelManager::selected() const {
    QList<Channel*> out;
    for (Channel* c : m_channels)
        if (c->isSelected()) out << c;
    return out;
}

int ChannelManager::recordingCount() const {
    return int(std::count_if(m_channels.begin(), m_channels.end(), [](Channel* c) { return c->isRecording(); }));
}

QString ChannelManager::sessionForFiles() const {
    return m_session.isEmpty() ? QStringLiteral("Session") : m_session;
}

Channel* ChannelManager::addChannel() {
    if (!canAdd()) return nullptr;
    static const int patterns[] = {0, 18, 21, 22, 13, 11, 23, 19};
    ChannelConfig cfg;
    cfg.name = QStringLiteral("TEST %1").arg(++m_nameCounter);
    cfg.pattern = patterns[(m_nameCounter - 1) % 8];
    cfg.audioVolume = 0.03 + 0.03 * ((m_nameCounter - 1) % 8);
    if (m_persist) cfg.out = AppSettings::defaultOutput();      // domyslne parametry nagrywania z okna Settings
    return addChannel(cfg);
}

Channel* ChannelManager::addChannel(const ChannelConfig& cfg, int at) {
    if (!canAdd()) return nullptr;
    auto* ch = new Channel(cfg, this);
    connect(ch, &Channel::changed, this, &ChannelManager::selectionChanged);
    connect(ch, &Channel::notice, this, &ChannelManager::notice);
    connect(ch, &Channel::configChanged, this, &ChannelManager::markDirty);
    connect(ch, &Channel::autoRecordRequested, this, [this, ch] { transport(ch, Transport::Rec); });
    connect(ch, &Channel::recordingFinished, this, [this, ch] {
        if (m_removeAfterStop.removeAll(ch)) removeChannel(ch);
        if (m_replacePending && recordingCount() == 0) {     // nagrania sfinalizowane - mozna podmienic liste
            m_replacePending = false;
            const QString path = m_replacePath;
            QTimer::singleShot(0, this, [this, path] { replaceListWhenIdle(path); });
        }
    });
    if (at < 0 || at > count()) at = count();
    m_channels.insert(at, ch);
    ch->startPreview();
    emit listChanged();
    setActive(ch);
    return ch;
}

void ChannelManager::removeChannel(Channel* ch) {
    const int i = indexOf(ch);
    if (i < 0) return;
    if (ch->isRecording()) {            // najpierw sfinalizuj plik, usuniecie nastapi po recordingFinished
        if (!m_removeAfterStop.contains(ch)) m_removeAfterStop << ch;
        ch->stopRecording();
        return;
    }
    m_channels.removeAt(i);
    if (m_monitored == ch) m_monitored = nullptr;
    if (m_active == ch) {
        m_active = m_channels.isEmpty() ? nullptr : m_channels.at(std::min(i, count() - 1));
        emit activeChanged();
    }
    emit notice(T("Removed channel %1").arg(ch->config().name));
    ch->stopPreview();
    ch->deleteLater();
    emit listChanged();
    emit selectionChanged();
}

void ChannelManager::removeChannels(const QList<Channel*>& list) {
    for (Channel* c : list) removeChannel(c);
}

void ChannelManager::duplicateChannels(const QList<Channel*>& list) {
    for (Channel* src : list) {
        if (!canAdd()) {
            emit notice(T("Limit of 16 channels reached — adding sources is disabled"));
            break;
        }
        ChannelConfig cfg = src->config();
        cfg.name = T("%1 copy").arg(cfg.name);
        cfg.live.type = QStringLiteral("off");      // ta sama nazwa NDI / ten sam port SRT nie moga dzialac dwa razy
        addChannel(cfg, indexOf(src) + 1);
    }
}

void ChannelManager::moveChannel(int from, int gap) {
    if (from < 0 || from >= count() || gap < 0 || gap > count()) return;
    const int to = gap > from ? gap - 1 : gap;
    if (to == from) return;
    m_channels.move(from, to);      // pipeline nie jest restartowany
    emit listChanged();
}

void ChannelManager::setActive(Channel* ch) {
    if (ch == m_active) return;
    m_active = ch;
    emit activeChanged();
}

void ChannelManager::selectAll() {
    for (Channel* c : m_channels) c->setSelected(true);
}

void ChannelManager::selectNone() {
    for (Channel* c : m_channels) c->setSelected(false);
}

void ChannelManager::invertSelection() {
    for (Channel* c : m_channels) c->setSelected(!c->isSelected());
}

void ChannelManager::transport(Channel* ch, Transport t) {
    transportGroup({ch}, t);
}

void ChannelManager::transportSelected(Transport t) {
    transportGroup(selected(), t);
}

void ChannelManager::transportGroup(const QList<Channel*>& list, Transport t) {
    if (list.isEmpty()) return;
    switch (t) {
    case Transport::Rec: {
        // Krok 1: dopiecie galezi zapisu we wszystkich kanalach (to trwa - proba enkodera, budowa galezi).
        // Krok 2: jedna chwila T, od ktorej wszystkie kanaly zaczynaja zapisywac. W pauzie REC wznawia nagranie.
        QList<Channel*> started, paused;
        for (Channel* c : list) {
            if (c->state() == ChannelState::Paused) paused << c;
            else if (c->startRecording(sessionForFiles(), indexOf(c) + 1)) started << c;
        }
        const quint64 now = Channel::clockNow();
        const quint64 startAt = now + (started.size() > 1 ? 80000000ULL : 0);    // 80 ms zapasu dla grupy
        for (Channel* c : started) c->beginRecordingAt(startAt);
        for (Channel* c : paused) c->resumeRecording(now);
        break;
    }
    case Transport::Pause: {
        // Grupa zachowuje sie jak jeden przycisk: jesli cokolwiek nagrywa - pauza tych kanalow, w przeciwnym razie wznowienie.
        bool anyRec = false;
        for (Channel* c : list) anyRec = anyRec || c->state() == ChannelState::Recording;
        const quint64 now = Channel::clockNow();
        for (Channel* c : list) {
            if (anyRec) c->pauseRecording(now); else c->resumeRecording(now);
        }
        break;
    }
    case Transport::Stop: {
        const quint64 now = Channel::clockNow();
        for (Channel* c : list) c->stopRecording(now);
        break;
    }
    case Transport::Split:
        for (Channel* c : list) c->split();
        break;
    }
}

// Pilnowanie miejsca, osobno dla kazdego wolumenu: ostrzezenie ponizej 30 min lub 10% wolnego,
// kontrolowany Stop (z finalizacja plikow) ponizej 1 GB - tylko dla kanalow z tego wolumenu.
void ChannelManager::checkDisks() {
    for (const VolumeInfo& v : DiskMonitor::volumes(m_channels)) {
        if (v.recording == 0) {
            m_warnedVolumes.removeAll(v.root);
            continue;
        }
        if (v.free < (1LL << 30)) {
            bool any = false;
            for (Channel* c : m_channels)
                if ((c->state() == ChannelState::Recording || c->state() == ChannelState::Paused) &&
                    DiskMonitor::rootFor(c->outputDir()) == v.root) {
                    c->stopRecording();
                    any = true;
                }
            if (any) emit notice(T("Disk %1 is almost full — recordings on it were stopped").arg(v.name()));
        } else if (!m_warnedVolumes.contains(v.root) &&
                   ((v.secondsLeft >= 0 && v.secondsLeft < 1800) || (v.total > 0 && v.free * 10 < v.total))) {
            m_warnedVolumes << v.root;
            emit notice(T("Disk %1 is filling up: about %2 of recording left").arg(v.name(), DiskMonitor::formatLeft(v.secondsLeft)));
        }
    }
}

void ChannelManager::splitToNewFolder(const QList<Channel*>& list) {
    const QDateTime when = QDateTime::currentDateTime();
    const QString tmpl = AppSettings::splitFolderTemplate();
    int n = 0;
    QString shown;
    for (Channel* c : list) {
        if (c->state() != ChannelState::Recording) continue;
        FileNameTemplate::Vars vars{sessionForFiles(), c->config().name, c->sourceText(), indexOf(c) + 1, when};
        QString name = FileNameTemplate::expand(tmpl, vars);
        name.remove(QRegularExpression(QStringLiteral("_?%0\\dd")));      // {part} nie ma sensu w nazwie folderu
        if (name.isEmpty()) name = when.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
        c->splitToFolder(QDir(c->outputDir()).filePath(name));
        shown = name;
        ++n;
    }
    if (n > 0) emit notice(T("Split to new folder: %1 (%2 channels)").arg(shown).arg(n));
}

void ChannelManager::setMonitored(Channel* ch) {
    if (m_monitored && m_channels.contains(m_monitored)) m_monitored->setMonitor(false);
    const bool off = !ch || ch == m_monitored;
    m_monitored = nullptr;
    if (!off) {
        ch->setMonitor(true, AppSettings::monitorDevice());
        if (ch->monitorOn()) m_monitored = ch;
    }
    emit selectionChanged();
}

void ChannelManager::stopAllRecording() {
    transportGroup(m_channels, Transport::Stop);
}

// ---------------------------------------------------------------- sesja i lista kanalow

namespace {
QJsonObject channelToJson(const Channel* ch) {
    const ChannelConfig& c = ch->config();
    const OutputConfig& o = c.out;
    const QString splitMode = o.splitSeconds > 0 ? QStringLiteral("time") : o.splitBytes > 0 ? QStringLiteral("size") : QStringLiteral("off");
    return QJsonObject{
        {QStringLiteral("name"), c.name},
        {QStringLiteral("selected"), ch->isSelected()},
        {QStringLiteral("input"), QJsonObject{{QStringLiteral("type"), c.in.type},
                                              {QStringLiteral("uri"), c.in.uri},
                                              {QStringLiteral("lossPolicy"), c.in.lossPolicy},
                                              {QStringLiteral("windowTitle"), c.in.windowTitle},
                                              {QStringLiteral("cameraMode"), c.in.cameraMode},
                                              {QStringLiteral("screenIndex"), c.in.screenIndex},
                                              {QStringLiteral("cameraPath"), c.in.cameraPath},
                                              {QStringLiteral("cameraName"), c.in.cameraName},
                                              {QStringLiteral("audio"), QJsonObject{{QStringLiteral("type"), c.in.audioType},
                                                                                    {QStringLiteral("device"), c.in.audioDevice},
                                                                                    {QStringLiteral("name"), c.in.audioName},
                                                                                    {QStringLiteral("gainDb"), c.in.gainDb}}},
                                              {QStringLiteral("pattern"), c.pattern},
                                              {QStringLiteral("audioWave"), c.audioWave},
                                              {QStringLiteral("audioVolume"), c.audioVolume},
                                              {QStringLiteral("width"), c.width},
                                              {QStringLiteral("height"), c.height},
                                              {QStringLiteral("fps"), c.fps}}},
        {QStringLiteral("output"), QJsonObject{
            {QStringLiteral("dir"), o.dir},
            {QStringLiteral("nameTemplate"), o.nameTemplate},
            {QStringLiteral("container"), o.container},
            {QStringLiteral("video"), QJsonObject{{QStringLiteral("codec"), QStringLiteral("h264")}, {QStringLiteral("encoder"), o.encoder},
                                                  {QStringLiteral("bitrateKbps"), o.videoKbps}}},
            {QStringLiteral("audio"), QJsonObject{{QStringLiteral("codec"), QStringLiteral("aac")}, {QStringLiteral("bitrateKbps"), o.audioKbps}}},
            {QStringLiteral("autoSplit"), QJsonObject{{QStringLiteral("mode"), splitMode}, {QStringLiteral("seconds"), o.splitSeconds},
                                                      {QStringLiteral("bytes"), double(o.splitBytes)}}}}},
        {QStringLiteral("live"), QJsonObject{{QStringLiteral("type"), c.live.type}, {QStringLiteral("ndiName"), c.live.ndiName},
                                             {QStringLiteral("srtAddress"), c.live.srtAddress}, {QStringLiteral("bitrateKbps"), c.live.srtKbps}}}};
}

// Wczytywanie odporne: nieznane pola sa ignorowane, brakujace dostaja wartosci domyslne.
ChannelConfig channelFromJson(const QJsonObject& j, int number) {
    ChannelConfig c;
    const ChannelConfig d;
    c.name = j.value(QStringLiteral("name")).toString().trimmed();
    if (c.name.isEmpty()) c.name = QStringLiteral("CH %1").arg(number);
    const QJsonObject in = j.value(QStringLiteral("input")).toObject();
    const QString it = in.value(QStringLiteral("type")).toString();
    c.in.type = it == QLatin1String("screen") || it == QLatin1String("window") || it == QLatin1String("camera") ||
                        it == QLatin1String("srt") || it == QLatin1String("rtsp") || it == QLatin1String("ndi") || it == QLatin1String("none")
                    ? it : QStringLiteral("test");
    c.in.uri = in.value(QStringLiteral("uri")).toString();
    const QString lp = in.value(QStringLiteral("lossPolicy")).toString();
    c.in.lossPolicy = lp == QLatin1String("pause") || lp == QLatin1String("split") ? lp : QStringLiteral("continue");
    c.in.windowTitle = in.value(QStringLiteral("windowTitle")).toString();
    c.in.cameraMode = in.value(QStringLiteral("cameraMode")).toString();
    c.in.screenIndex = qBound(0, in.value(QStringLiteral("screenIndex")).toInt(), 15);
    c.in.cameraPath = in.value(QStringLiteral("cameraPath")).toString();
    c.in.cameraName = in.value(QStringLiteral("cameraName")).toString();
    const QJsonObject au = in.value(QStringLiteral("audio")).toObject();
    const QString at = au.value(QStringLiteral("type")).toString();
    c.in.audioType = at == QLatin1String("none") || at == QLatin1String("loopback") || at == QLatin1String("device") ? at : QStringLiteral("test");
    c.in.audioDevice = au.value(QStringLiteral("device")).toString();
    c.in.audioName = au.value(QStringLiteral("name")).toString();
    c.in.gainDb = qBound(-30, au.value(QStringLiteral("gainDb")).toInt(), 30);
    c.pattern = in.value(QStringLiteral("pattern")).toInt(d.pattern);
    c.audioWave = in.value(QStringLiteral("audioWave")).toInt(d.audioWave);
    c.audioVolume = qBound(0.0, in.value(QStringLiteral("audioVolume")).toDouble(d.audioVolume), 1.0);
    c.width = qBound(160, in.value(QStringLiteral("width")).toInt(d.width), 7680);
    c.height = qBound(120, in.value(QStringLiteral("height")).toInt(d.height), 4320);
    c.fps = qBound(1, in.value(QStringLiteral("fps")).toInt(d.fps), 120);
    const QJsonObject out = j.value(QStringLiteral("output")).toObject();
    c.out.dir = out.value(QStringLiteral("dir")).toString();
    c.out.nameTemplate = out.value(QStringLiteral("nameTemplate")).toString();
    c.out.container = out.value(QStringLiteral("container")).toString() == QLatin1String("ts") ? QStringLiteral("ts") : QStringLiteral("mkv");
    const QJsonObject v = out.value(QStringLiteral("video")).toObject();
    c.out.encoder = v.value(QStringLiteral("encoder")).toString(d.out.encoder);
    c.out.videoKbps = qBound(500, v.value(QStringLiteral("bitrateKbps")).toInt(d.out.videoKbps), 200000);
    c.out.audioKbps = qBound(64, out.value(QStringLiteral("audio")).toObject().value(QStringLiteral("bitrateKbps")).toInt(d.out.audioKbps), 512);
    const QJsonObject sp = out.value(QStringLiteral("autoSplit")).toObject();
    const QString mode = sp.value(QStringLiteral("mode")).toString();
    c.out.splitSeconds = mode == QLatin1String("time") ? qMax(0, sp.value(QStringLiteral("seconds")).toInt()) : 0;
    c.out.splitBytes = mode == QLatin1String("size") ? qMax<qint64>(0, qint64(sp.value(QStringLiteral("bytes")).toDouble())) : 0;
    const QJsonObject live = j.value(QStringLiteral("live")).toObject();
    const QString lt = live.value(QStringLiteral("type")).toString();
    c.live.type = lt == QLatin1String("ndi") || lt == QLatin1String("srt") ? lt : QStringLiteral("off");
    c.live.ndiName = live.value(QStringLiteral("ndiName")).toString();
    c.live.srtAddress = live.value(QStringLiteral("srtAddress")).toString();
    c.live.srtKbps = qBound(500, live.value(QStringLiteral("bitrateKbps")).toInt(d.live.srtKbps), 100000);
    return c;
}

bool writeJson(const QString& path, const QJsonObject& root, QString* error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);                       // zapis atomowy: plik tymczasowy + podmiana
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson()) < 0 || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool readJson(const QString& path, QJsonObject* root, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        if (error) *error = pe.error != QJsonParseError::NoError ? pe.errorString() : T("this is not a channel list file");
        return false;
    }
    *root = doc.object();
    return true;
}
}  // namespace

QJsonObject ChannelManager::toJson() const {
    QJsonArray arr;
    for (const Channel* c : m_channels) arr.append(channelToJson(c));
    return QJsonObject{{QStringLiteral("schema"), QStringLiteral("isorec.list")},
                       {QStringLiteral("version"), 1},
                       {QStringLiteral("createdWith"), QStringLiteral("IsoRecorder " APP_VERSION)},
                       {QStringLiteral("session"), m_session},
                       {QStringLiteral("channels"), arr}};
}

void ChannelManager::clearChannels() {
    const QList<Channel*> old = m_channels;
    m_channels.clear();
    m_removeAfterStop.clear();
    m_active = nullptr;
    m_monitored = nullptr;
    for (Channel* c : old) {
        c->stopPreview();
        c->deleteLater();
    }
    emit activeChanged();
    emit listChanged();
    emit selectionChanged();
}

bool ChannelManager::fromJson(const QJsonObject& root, QString* error) {
    if (root.value(QStringLiteral("schema")).toString() != QLatin1String("isorec.list")) {
        if (error) *error = T("this is not a channel list file");
        return false;
    }
    if (recordingCount() > 0) {
        if (error) *error = T("recordings are in progress");
        return false;
    }
    m_loading = true;
    clearChannels();
    m_session = root.value(QStringLiteral("session")).toString();
    const QJsonArray arr = root.value(QStringLiteral("channels")).toArray();
    int n = 0;
    for (const QJsonValue& v : arr) {
        if (!canAdd()) break;
        const QJsonObject j = v.toObject();
        if (Channel* ch = addChannel(channelFromJson(j, ++n))) ch->setSelected(j.value(QStringLiteral("selected")).toBool());
    }
    m_nameCounter = count();
    if (!m_channels.isEmpty()) setActive(m_channels.first());
    m_loading = false;
    return true;
}

void ChannelManager::setSession(const QString& name) {
    const QString n = name.trimmed();
    if (n == m_session) return;
    m_session = n;
    markDirty();
}

void ChannelManager::markDirty() {
    if (m_loading) return;
    m_dirty = true;
    if (m_persist) m_autosave.start();       // 2 s po ostatniej zmianie
    emit sessionChanged();
}

QString ChannelManager::settingsOrg() {
    const QString profile = qEnvironmentVariable("ISOREC_PROFILE");
    return profile.isEmpty() ? QStringLiteral("IsoRecorder") : QStringLiteral("IsoRecorder-") + profile;
}

QString ChannelManager::autosavePath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(settingsOrg() + QStringLiteral("/autosave.json"));
}

void ChannelManager::writeAutosave() {
    if (!m_persist || !m_dirty) return;
    QJsonObject root = toJson();
    root.insert(QStringLiteral("listPath"), m_listPath);
    if (writeJson(autosavePath(), root, nullptr)) {
        m_autosavedAt = QTime::currentTime();
        emit sessionChanged();
    }
}

void ChannelManager::discardAutosave() {
    m_autosave.stop();
    QFile::remove(autosavePath());
    m_autosavedAt = QTime();
}

bool ChannelManager::restoreAutosave(QString* error) {
    QJsonObject root;
    if (!readJson(autosavePath(), &root, error) || !fromJson(root, error)) return false;
    m_listPath = root.value(QStringLiteral("listPath")).toString();
    m_dirty = true;
    emit sessionChanged();
    emit notice(T("Unsaved channel list restored"));
    return true;
}

void ChannelManager::remember(const QString& path) {
    if (!m_persist) return;
    QSettings s(ChannelManager::settingsOrg(), QStringLiteral("IsoRecorder"));
    QStringList recent = s.value(QStringLiteral("recentLists")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 8) recent.removeLast();
    s.setValue(QStringLiteral("recentLists"), recent);
    s.setValue(QStringLiteral("lastList"), path);
}

QStringList ChannelManager::recentLists() const {
    QStringList out;
    const QSettings s(ChannelManager::settingsOrg(), QStringLiteral("IsoRecorder"));
    for (const QString& p : s.value(QStringLiteral("recentLists")).toStringList())
        if (QFileInfo::exists(p)) out << p;
    return out;
}

bool ChannelManager::exportList(const QString& path, QString* error) const {
    return writeJson(path, toJson(), error);
}

bool ChannelManager::saveList(const QString& path, QString* error) {
    if (m_session.isEmpty()) {              // sesja bez nazwy bierze ja z nazwy pliku
        QString base = QFileInfo(path).fileName();
        if (base.endsWith(QLatin1String(ListSuffix), Qt::CaseInsensitive)) base.chop(int(qstrlen(ListSuffix)));
        m_session = base;
    }
    if (!writeJson(path, toJson(), error)) return false;
    m_listPath = QDir::fromNativeSeparators(path);
    m_dirty = false;
    discardAutosave();
    remember(m_listPath);
    emit sessionChanged();
    emit notice(T("List saved — %1").arg(QFileInfo(path).fileName()));
    return true;
}

bool ChannelManager::loadList(const QString& path, QString* error) {
    QJsonObject root;
    if (!readJson(path, &root, error) || !fromJson(root, error)) return false;
    m_listPath = QDir::fromNativeSeparators(path);
    m_dirty = false;
    discardAutosave();
    remember(m_listPath);
    emit sessionChanged();
    emit notice(T("List loaded — %1 (%2 channels)").arg(QFileInfo(path).fileName()).arg(count()));
    return true;
}

void ChannelManager::newList() {
    if (recordingCount() > 0) return;
    m_loading = true;
    clearChannels();
    m_loading = false;
    m_session.clear();
    m_listPath.clear();
    m_nameCounter = 0;
    m_dirty = false;
    discardAutosave();
    if (m_persist) QSettings(ChannelManager::settingsOrg(), QStringLiteral("IsoRecorder")).remove(QStringLiteral("lastList"));
    emit sessionChanged();
}

void ChannelManager::replaceListWhenIdle(const QString& path) {
    auto run = [this, path] {
        if (path.isEmpty()) {
            newList();
            return;
        }
        QString err;
        if (!loadList(path, &err)) emit notice(T("Cannot load the list: %1").arg(err));
    };
    if (recordingCount() == 0) {
        run();
        return;
    }
    m_replacePending = true;
    m_replacePath = path;
    stopAllRecording();
}
