#include "ChannelManager.h"

#include "Lang.h"

#include <algorithm>

ChannelManager::ChannelManager(QObject* parent) : QObject(parent) {}

int ChannelManager::selectedCount() const {
    return int(std::count_if(m_channels.begin(), m_channels.end(), [](Channel* c) { return c->isSelected(); }));
}

Channel* ChannelManager::addChannel() {
    if (!canAdd()) return nullptr;
    static const int patterns[] = {0, 18, 21, 22, 13, 11, 23, 19};
    ChannelConfig cfg;
    cfg.name = QStringLiteral("TEST %1").arg(++m_nameCounter);
    cfg.pattern = patterns[(m_nameCounter - 1) % 8];
    cfg.audioVolume = 0.03 + 0.03 * ((m_nameCounter - 1) % 8);
    auto* ch = new Channel(cfg, this);
    connect(ch, &Channel::changed, this, &ChannelManager::selectionChanged);
    m_channels.append(ch);
    ch->startPreview();
    emit listChanged();
    setActive(ch);
    return ch;
}

void ChannelManager::removeChannel(Channel* ch) {
    const int i = indexOf(ch);
    if (i < 0) return;
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

void ChannelManager::moveActive(int delta) {
    const int from = indexOf(m_active);
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= count()) return;
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

void ChannelManager::transport(Channel*, Transport) {
    emit notice(T("Recording (Rec / Pause / Stop / Split) will be available from version %1").arg(QLatin1String(Roadmap::recording)));
}

void ChannelManager::transportSelected(Transport t) {
    if (selectedCount() > 0) transport(nullptr, t);
}
