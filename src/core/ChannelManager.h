#pragma once
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>

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
    QList<Channel*> selected() const;
    int selectedCount() const { return int(selected().size()); }
    int recordingCount() const;
    QString session() const { return m_session; }          // puste = sesja bez nazwy
    QString sessionForFiles() const;

    Channel* addChannel();
    Channel* addChannel(const ChannelConfig& cfg, int at = -1);
    void removeChannel(Channel* ch);                        // nagrywajacy kanal jest najpierw finalizowany
    void removeChannels(const QList<Channel*>& list);
    void duplicateChannels(const QList<Channel*>& list);
    void moveActive(int delta);
    void setActive(Channel* ch);
    void selectAll();
    void selectNone();
    void invertSelection();
    void transport(Channel* ch, Transport t);               // jeden kanal
    void transportSelected(Transport t);                    // zaznaczone
    void stopAllRecording();

signals:
    void listChanged();        // dodanie / usuniecie / kolejnosc
    void activeChanged();
    void selectionChanged();   // takze zmiana stanu ktoregokolwiek kanalu
    void statsChanged();       // co 1 s podczas nagrywania
    void notice(const QString& text);

private:
    QList<Channel*> m_channels;
    QList<Channel*> m_removeAfterStop;
    Channel* m_active = nullptr;
    int m_nameCounter = 0;
    QString m_session;
    QStringList m_warnedVolumes;    // wolumeny, o ktorych juz ostrzegalismy
    void checkDisks();
    QTimer m_poll;
};
