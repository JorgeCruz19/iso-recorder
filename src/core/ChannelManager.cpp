#include "ChannelManager.h"

#include "DiskMonitor.h"
#include "Lang.h"

#include <algorithm>

ChannelManager::ChannelManager(QObject* parent) : QObject(parent) {
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        for (Channel* c : m_channels) c->pollRecording();
        checkDisks();
        emit statsChanged();
    });
    m_poll.start();
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
    return addChannel(cfg);
}

Channel* ChannelManager::addChannel(const ChannelConfig& cfg, int at) {
    if (!canAdd()) return nullptr;
    auto* ch = new Channel(cfg, this);
    connect(ch, &Channel::changed, this, &ChannelManager::selectionChanged);
    connect(ch, &Channel::notice, this, &ChannelManager::notice);
    connect(ch, &Channel::recordingFinished, this, [this, ch] {
        if (m_removeAfterStop.removeAll(ch)) removeChannel(ch);
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

void ChannelManager::moveActive(int delta) {
    const int from = indexOf(m_active);
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= count()) return;
    m_channels.move(from, to);      // pipeline nie jest restartowany
    emit listChanged();
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
    switch (t) {
    case Transport::Rec:        // w pauzie REC wznawia nagranie
        if (ch->state() == ChannelState::Paused) ch->resumeRecording();
        else ch->startRecording(sessionForFiles(), indexOf(ch) + 1);
        break;
    case Transport::Stop: ch->stopRecording(); break;
    case Transport::Pause:      // przelacznik: pauza / wznowienie
        if (ch->state() == ChannelState::Paused) ch->resumeRecording();
        else ch->pauseRecording();
        break;
    case Transport::Split: ch->split(); break;
    }
}

void ChannelManager::transportSelected(Transport t) {
    const QList<Channel*> sel = selected();
    if (sel.isEmpty()) return;
    if (t == Transport::Pause) {
        // Grupa zachowuje sie jak jeden przycisk: jesli cokolwiek nagrywa - pauza tych kanalow, w przeciwnym razie wznowienie.
        bool anyRec = false;
        for (Channel* c : sel) anyRec = anyRec || c->state() == ChannelState::Recording;
        for (Channel* c : sel) {
            if (anyRec) c->pauseRecording(); else c->resumeRecording();
        }
        return;
    }
    for (Channel* c : sel) transport(c, t);
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

void ChannelManager::stopAllRecording() {
    for (Channel* c : m_channels) c->stopRecording();
}
