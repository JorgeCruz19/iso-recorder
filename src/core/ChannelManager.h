#pragma once
#include <QList>
#include <QObject>

#include "Channel.h"

enum class Transport { Rec, Pause, Stop, Split };

class ChannelManager : public QObject {
    Q_OBJECT
public:
    static constexpr int MaxChannels = 16;
    explicit ChannelManager(QObject* parent = nullptr);

    const QList<Channel*>& channels() const { return m_channels; }
    int count() const { return int(m_channels.size()); }
    bool canAdd() const { return count() < MaxChannels; }
    Channel* active() const { return m_active; }
    int indexOf(Channel* ch) const { return int(m_channels.indexOf(ch)); }
    int selectedCount() const;

    Channel* addChannel();
    void removeChannel(Channel* ch);
    void moveActive(int delta);
    void setActive(Channel* ch);
    void selectAll();
    void selectNone();
    void invertSelection();
    void transport(Channel* ch, Transport t);       // kanal
    void transportSelected(Transport t);            // zaznaczone

signals:
    void listChanged();        // dodanie / usuniecie / kolejnosc
    void activeChanged();
    void selectionChanged();
    void notice(const QString& text);

private:
    QList<Channel*> m_channels;
    Channel* m_active = nullptr;
    int m_nameCounter = 0;
};
