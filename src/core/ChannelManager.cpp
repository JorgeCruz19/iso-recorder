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
#include <QStorageInfo>

#include <algorithm>

#include <windows.h>

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
    m_schedTick.setInterval(250);
    connect(&m_schedTick, &QTimer::timeout, this, &ChannelManager::schedulerTick);
    m_schedTick.start();
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(2000);
    connect(&m_autosave, &QTimer::timeout, this, &ChannelManager::writeAutosave);
    connect(this, &ChannelManager::listChanged, this, &ChannelManager::markDirty);    // dodanie, usuniecie, kolejnosc
}

// Scheduler. Kazda regula to okno "od - do". Na poczatku okna kanal zaczyna nagrywac, na koncu konczy, w trakcie moze
// dzielic plik. Stan jest porownywany z zegarem sciennym 4 razy na sekunde, wiec uspienie komputera, przestawienie
// zegara i zmiana czasu niczego nie gubia ani nie powtarzaja: okno jest "obsluzone" dokladnie raz (po poczatku okna).
// Kanaly, ktore maja ruszyc w tej samej chwili, startuja jedna grupa (wspolna chwila T).
void ChannelManager::schedulerTick() {
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = m_sched.begin(); it != m_sched.end();)
        it = m_channels.contains(it.key()) ? std::next(it) : m_sched.erase(it);
    QList<Channel*> toStart, toStop;
    QList<bool> startLate;
    bool pending = false;
    for (Channel* c : m_channels) {
        const ScheduleConfig& sc = c->config().sched;
        SchedState& st = m_sched[c];
        const Schedule::Window w = sc.active() ? Schedule::windowAt(sc, now) : Schedule::Window{};
        const bool recording = c->state() == ChannelState::Recording || c->state() == ChannelState::Paused;
        if (w.valid() && st.handled == w.start && st.stopAt.isValid()) st.stopAt = w.end;      // zmieniona godzina konca
        if (st.stopAt.isValid() && now >= st.stopAt) {
            if (recording && st.stopArmed != st.stopAt) {
                toStop << c;
                emit notice(T("%1: scheduled stop").arg(c->config().name));
            }
            st.stopAt = QDateTime();
        } else if (st.stopAt.isValid() && recording && st.stopArmed != st.stopAt && now.msecsTo(st.stopAt) <= 300) {
            // Koniec okna wypada przed nastepnym taktem: Stop dokladnie o tej chwili, a nie do 250 ms pozniej.
            st.stopArmed = st.stopAt;
            const QDateTime at = st.stopAt;
            QTimer* t = new QTimer(this);
            t->setTimerType(Qt::PreciseTimer);
            t->setSingleShot(true);
            connect(t, &QTimer::timeout, this, [this, c, at, t] {
                t->deleteLater();
                if (!m_channels.contains(c) || m_sched.value(c).stopArmed != at) return;
                if (c->state() == ChannelState::Recording || c->state() == ChannelState::Paused) {
                    emit notice(T("%1: scheduled stop").arg(c->config().name));
                    transportGroup({c}, Transport::Stop);
                }
            });
            t->start(int(qMax<qint64>(0, now.msecsTo(st.stopAt))));
        }
        if (w.valid() && st.handled != w.start && (!st.retryAfter.isValid() || now >= st.retryAfter)) {
            const bool late = w.start.msecsTo(now) > 3000;
            const qint64 split = sc.rules.at(w.rule).splitSeconds;
            if (late && !sc.catchUp) {
                st.handled = w.start;
                emit notice(T("%1: scheduled start missed — late start is off").arg(c->config().name));
            } else if (c->isRecording()) {      // konflikt: Start w trakcie nagrywania - nagranie trwa, Stop na koncu okna
                st.handled = w.start;
                st.stopAt = w.end;
                st.lastSplit = split > 0 ? w.start.secsTo(now) / split : 0;
                emit notice(T("%1: scheduled start ignored — already recording").arg(c->config().name));
            } else if (c->state() == ChannelState::Preview) {
                toStart << c;
                startLate << late;
                st.lastSplit = split > 0 ? w.start.secsTo(now) / split : 0;
            }       // kanal w stanie bledu albo bez sygnalu: kolejna proba przy nastepnym takcie, dopoki trwa okno
        }
        if (w.valid() && st.handled == w.start && st.stopAt.isValid() && c->state() == ChannelState::Recording) {
            const qint64 split = sc.rules.at(w.rule).splitSeconds;
            const qint64 k = split > 0 ? w.start.secsTo(now) / split : 0;
            if (k > st.lastSplit) {
                st.lastSplit = k;
                c->split();
            }
        }
    }
    if (!toStop.isEmpty()) transportGroup(toStop, Transport::Stop);
    // Przygotowanie: dopiecie galezi zapisu (proba enkodera, budowa galezi) trwa okolo sekundy, wiec robimy to do 2 s
    // przed poczatkiem okna, a sam zapis rusza dokladnie o zaplanowanej chwili - wspolnej dla kanalow z tym samym startem.
    QHash<qint64, QList<Channel*>> armed;
    for (Channel* c : m_channels) {
        const ScheduleConfig& sc = c->config().sched;
        if (!sc.active() || c->state() != ChannelState::Preview) continue;
        SchedState& st = m_sched[c];
        const Schedule::Window next = Schedule::nextWindow(sc, now);
        if (!next.valid() || st.handled == next.start || now.msecsTo(next.start) > 2000) continue;
        if (st.retryAfter.isValid() && now < st.retryAfter) continue;
        if (!c->startRecording(sessionForFiles(), indexOf(c) + 1)) {
            st.retryAfter = now.addSecs(5);
            continue;
        }
        st.handled = next.start;
        st.stopAt = next.end;
        st.lastSplit = 0;
        st.retryAfter = QDateTime();
        armed[next.start.toMSecsSinceEpoch()] << c;
    }
    for (auto it = armed.begin(); it != armed.end(); ++it) {
        const QList<Channel*> group = it.value();
        QTimer* t = new QTimer(this);
        t->setTimerType(Qt::PreciseTimer);
        t->setSingleShot(true);
        connect(t, &QTimer::timeout, this, [this, group, t] {
            t->deleteLater();
            const quint64 at = Channel::clockNow();
            for (Channel* c : group) {
                if (!m_channels.contains(c) || !c->isRecording()) continue;
                c->beginRecordingAt(at);
                emit notice(T("%1: scheduled recording started").arg(c->config().name));
            }
        });
        t->start(int(qMax<qint64>(0, QDateTime::currentDateTime().msecsTo(QDateTime::fromMSecsSinceEpoch(it.key())))));
    }
    if (!toStart.isEmpty()) {
        transportGroup(toStart, Transport::Rec);
        for (int i = 0; i < toStart.size(); ++i) {
            Channel* c = toStart.at(i);
            SchedState& st = m_sched[c];
            const Schedule::Window w = Schedule::windowAt(c->config().sched, now);
            if (c->isRecording() && w.valid()) {
                st.handled = w.start;
                st.stopAt = w.end;
                st.retryAfter = QDateTime();
                emit notice((startLate.at(i) ? T("%1: scheduled recording started late") : T("%1: scheduled recording started")).arg(c->config().name));
            } else {
                st.retryAfter = now.addSecs(5);     // np. brak enkodera - nie ponawiaj 4 razy na sekunde
            }
        }
    }
    // Status do pokazania w interfejsie, bramka wyjscia na zywo w trybie "jak w harmonogramie", blokada uspienia.
    for (Channel* c : m_channels) {
        const ScheduleConfig& sc = c->config().sched;
        const SchedState& st = m_sched[c];
        const Schedule::Window w = sc.active() ? Schedule::windowAt(sc, now) : Schedule::Window{};
        const bool inWindow = w.valid() && st.handled == w.start && st.stopAt.isValid();
        const Schedule::Window next = sc.active() ? Schedule::nextWindow(sc, now) : Schedule::Window{};
        // okno trwa, ale kanal jeszcze nie ruszyl (blad zrodla) - nadal "czeka" na start z tego okna
        const QDateTime nextStart = w.valid() && st.handled != w.start ? w.start : next.start;
        c->setScheduleStatus(c->isRecording() ? QDateTime() : nextStart, inWindow && c->isRecording() ? st.stopAt : QDateTime());
        c->setLiveGate(inWindow);
        pending = pending || w.valid() || next.valid();
    }
    const bool block = pending || recordingCount() > 0;
    if (block != m_sleepBlocked) {      // komputer nie zasypia, gdy trwa nagranie albo czeka zaplanowane
        m_sleepBlocked = block;
        SetThreadExecutionState(ES_CONTINUOUS | (block ? ES_SYSTEM_REQUIRED : 0));
    }
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
    if (m_persist) {
        const AppSettings::DefaultFormat f = AppSettings::defaultFormat();      // domyslny rozmiar obrazu i klatkaz z okna Settings
        cfg.width = f.width;
        cfg.height = f.height;
        cfg.fps = f.fps;
    }
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

namespace {
// < 0: a przed b
int compareChannels(const Channel* a, const Channel* b, SortKey key) {
    auto stateRank = [](const Channel* c) {
        switch (c->state()) {
        case ChannelState::Recording: case ChannelState::Finalizing: return 0;
        case ChannelState::Paused: return 1;
        case ChannelState::Preview: return c->waitingForSchedule() ? 3 : 2;
        case ChannelState::Error: return 4;
        default: return 5;
        }
    };
    switch (key) {
    case SortKey::Name: return QString::localeAwareCompare(a->config().name.toLower(), b->config().name.toLower());
    case SortKey::Source: return QString::localeAwareCompare(a->sourceText().toLower(), b->sourceText().toLower());
    case SortKey::Format: {
        const qint64 pa = qint64(a->config().width) * a->config().height, pb = qint64(b->config().width) * b->config().height;
        if (pa != pb) return pa < pb ? -1 : 1;
        return a->config().fps - b->config().fps;
    }
    case SortKey::State: return stateRank(a) - stateRank(b);
    }
    return 0;
}
}  // namespace

int ChannelManager::sortedBy(SortKey key) const {
    bool asc = true, desc = true, differ = false;
    for (int i = 1; i < m_channels.size(); ++i) {
        const int c = compareChannels(m_channels.at(i - 1), m_channels.at(i), key);
        asc = asc && c <= 0;
        desc = desc && c >= 0;
        differ = differ || c != 0;
    }
    return !differ ? 0 : asc ? 1 : desc ? -1 : 0;
}

void ChannelManager::sortChannels(SortKey key) {
    if (m_channels.size() < 2) return;
    const bool descending = sortedBy(key) == 1;
    std::stable_sort(m_channels.begin(), m_channels.end(), [key, descending](const Channel* a, const Channel* b) {
        const int c = compareChannels(a, b, key);
        return descending ? c > 0 : c < 0;
    });
    emit listChanged();      // pipeline'y nie sa restartowane, zmieniaja sie tylko numery kanalow
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
    // Folder zapasowy: gdy na dysku folderu glownego zostaje mniej niz ustawiony prog, nastepna czesc pliku idzie do
    // folderu zapasowego. Sprawdzane przed regula "Stop ponizej 1 GB", zeby nagranie zdazylo sie przeniesc.
    for (Channel* c : m_channels) {
        if (!c->canSwitchToSpare()) continue;
        const qint64 free = m_fakeFree >= 0 ? m_fakeFree : QStorageInfo(DiskMonitor::rootFor(c->recordingBaseDir())).bytesAvailable();
        if (free < qint64(c->config().out.spareMinMb) * 1024 * 1024) c->switchToSpare();
    }
    for (const VolumeInfo& v : DiskMonitor::volumes(m_channels)) {
        if (v.recording == 0) {
            m_warnedVolumes.removeAll(v.root);
            continue;
        }
        if (v.free < (1LL << 30)) {
            bool any = false;
            for (Channel* c : m_channels)
                if ((c->state() == ChannelState::Recording || c->state() == ChannelState::Paused) &&
                    DiskMonitor::rootFor(c->recordingBaseDir()) == v.root) {
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
        c->splitToFolder(QDir(c->recordingBaseDir()).filePath(name));
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
                                              {QStringLiteral("deckIndex"), c.in.deckIndex},
                                              {QStringLiteral("deckName"), c.in.deckName},
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
            {QStringLiteral("second"), QJsonObject{{QStringLiteral("mode"), o.dir2Mode}, {QStringLiteral("dir"), o.dir2},
                                                   {QStringLiteral("minFreeMb"), o.spareMinMb}}},
            {QStringLiteral("container"), o.container},
            {QStringLiteral("growing"), o.growing},
            {QStringLiteral("video"), QJsonObject{{QStringLiteral("codec"), o.codec}, {QStringLiteral("encoder"), o.encoder},
                                                  {QStringLiteral("bitrateKbps"), o.videoKbps}}},
            {QStringLiteral("audio"), QJsonObject{{QStringLiteral("codec"), QStringLiteral("aac")}, {QStringLiteral("bitrateKbps"), o.audioKbps}}},
            {QStringLiteral("autoSplit"), QJsonObject{{QStringLiteral("mode"), splitMode}, {QStringLiteral("seconds"), o.splitSeconds},
                                                      {QStringLiteral("bytes"), double(o.splitBytes)}}}}},
        {QStringLiteral("live"), QJsonObject{{QStringLiteral("type"), c.live.type}, {QStringLiteral("ndiName"), c.live.ndiName},
                                             {QStringLiteral("srtAddress"), c.live.srtAddress}, {QStringLiteral("bitrateKbps"), c.live.srtKbps},
                                             {QStringLiteral("when"), c.live.when}}},
        {QStringLiteral("schedule"), Schedule::toJson(c.sched)}};
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
                        it == QLatin1String("srt") || it == QLatin1String("rtsp") || it == QLatin1String("ndi") || it == QLatin1String("none") || it == QLatin1String("decklink") || it == QLatin1String("whep")
                    ? it : QStringLiteral("test");
    c.in.uri = in.value(QStringLiteral("uri")).toString();
    const QString lp = in.value(QStringLiteral("lossPolicy")).toString();
    c.in.lossPolicy = lp == QLatin1String("pause") || lp == QLatin1String("split") ? lp : QStringLiteral("continue");
    c.in.windowTitle = in.value(QStringLiteral("windowTitle")).toString();
    c.in.cameraMode = in.value(QStringLiteral("cameraMode")).toString();
    c.in.screenIndex = qBound(0, in.value(QStringLiteral("screenIndex")).toInt(), 15);
    c.in.deckIndex = qBound(0, in.value(QStringLiteral("deckIndex")).toInt(), 63);
    c.in.deckName = in.value(QStringLiteral("deckName")).toString();
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
    const QJsonObject sec = out.value(QStringLiteral("second")).toObject();
    const QString sm = sec.value(QStringLiteral("mode")).toString();
    c.out.dir2Mode = sm == QLatin1String("spare") || sm == QLatin1String("mirror") ? sm : QStringLiteral("off");
    c.out.dir2 = sec.value(QStringLiteral("dir")).toString();
    c.out.spareMinMb = qBound(1024, sec.value(QStringLiteral("minFreeMb")).toInt(d.out.spareMinMb), 1024 * 1024);
    const QString cont = out.value(QStringLiteral("container")).toString();
    c.out.container = cont == QLatin1String("ts") || cont == QLatin1String("mp4") || cont == QLatin1String("mov") ? cont : QStringLiteral("mkv");
    c.out.growing = out.value(QStringLiteral("growing")).toBool(false);
    const QJsonObject v = out.value(QStringLiteral("video")).toObject();
    c.out.encoder = v.value(QStringLiteral("encoder")).toString(d.out.encoder);
    const QString codec = v.value(QStringLiteral("codec")).toString();
    c.out.codec = codec == QLatin1String("h265") || codec == QLatin1String("av1") || codec == QLatin1String("prores") ? codec : QStringLiteral("h264");
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
    c.live.when = live.value(QStringLiteral("when")).toString() == QLatin1String("schedule") ? QStringLiteral("schedule") : QStringLiteral("always");
    c.sched = Schedule::fromJson(j.value(QStringLiteral("schedule")).toObject());
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
