#pragma once
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTime>
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
    void moveChannel(int from, int gap);                    // gap = pozycja wstawienia 0..count (przed kanalem o tym indeksie)
    void setActive(Channel* ch);
    void selectAll();
    void selectNone();
    void invertSelection();
    void transport(Channel* ch, Transport t);               // jeden kanal
    void transportSelected(Transport t);                    // zaznaczone
    // Grupa kanalow ze wspolna chwila T: start, pauza, wznowienie i stop wypadaja we wszystkich kanalach w tym samym momencie.
    void transportGroup(const QList<Channel*>& list, Transport t);
    void stopAllRecording();
    // Split z przejsciem do nowego folderu: nazwa folderu z szablonu w ustawieniach, wspolna dla calej grupy.
    void splitToNewFolder(const QList<Channel*>& list);
    // Odsluch: najwyzej jeden kanal naraz gra na karcie wybranej w ustawieniach.
    Channel* monitored() const { return m_monitored; }
    void setMonitored(Channel* ch);         // nullptr = wylacz; ten sam kanal ponownie = wylacz

    // --- Sesja i lista kanalow (plik <sesja>.isorec.json) ---
    void setSession(const QString& name);
    QString listPath() const { return m_listPath; }
    bool isDirty() const { return m_dirty; }
    QJsonObject toJson() const;
    bool saveList(const QString& path, QString* error = nullptr);
    bool exportList(const QString& path, QString* error = nullptr) const;   // zapis bez zmiany stanu sesji
    bool loadList(const QString& path, QString* error = nullptr);
    void newList();
    // Wczytanie / nowa lista przy trwajacych nagraniach: najpierw finalizacja plikow, potem zmiana listy.
    void replaceListWhenIdle(const QString& path);      // puste = nowa lista
    // Autozapis i pamiec ostatnich list; wylaczone w trybach testowych (--demo, --list).
    void setPersistence(bool on) { m_persist = on; }
    bool persistence() const { return m_persist; }
    static QString autosavePath();
    // Nazwa magazynu ustawien. Zmienna srodowiskowa ISOREC_PROFILE daje osobny, izolowany profil (testy).
    static QString settingsOrg();
    bool restoreAutosave(QString* error = nullptr);
    void discardAutosave();
    QTime autosavedAt() const { return m_autosavedAt; }
    QStringList recentLists() const;
    static constexpr const char* ListSuffix = ".isorec.json";

signals:
    void listChanged();        // dodanie / usuniecie / kolejnosc
    void activeChanged();
    void selectionChanged();   // takze zmiana stanu ktoregokolwiek kanalu
    void statsChanged();       // co 1 s podczas nagrywania
    void notice(const QString& text);
    void sessionChanged();     // nazwa sesji, plik listy, niezapisane zmiany, autozapis

private:
    QList<Channel*> m_channels;
    QList<Channel*> m_removeAfterStop;
    Channel* m_active = nullptr;
    Channel* m_monitored = nullptr;
    int m_nameCounter = 0;
    QString m_session;
    QStringList m_warnedVolumes;    // wolumeny, o ktorych juz ostrzegalismy
    void checkDisks();
    bool fromJson(const QJsonObject& root, QString* error);
    void clearChannels();
    void markDirty();
    void writeAutosave();
    void remember(const QString& path);
    QString m_listPath;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_persist = false;
    bool m_replacePending = false;
    QString m_replacePath;
    QTimer m_autosave;
    QTime m_autosavedAt;
    QTimer m_poll;
    // --- Scheduler: co 250 ms porownuje zegar scienny z regulami kanalow (odporne na uspienie i zmiane czasu) ---
    struct SchedState {
        QDateTime handled;      // poczatek okna, ktore zostalo juz obsluzone (start wykonany, pominiety albo zignorowany)
        QDateTime stopAt;       // koniec tego okna: wtedy Stop; niewazne = nic do zatrzymania
        qint64 lastSplit = 0;   // numer ostatniego wykonanego podzialu w oknie
        QDateTime retryAfter;   // nieudany start: nastepna proba
        QDateTime stopArmed;    // koniec okna, dla ktorego dokladny Stop jest juz nastawiony
    };
    QHash<Channel*, SchedState> m_sched;
    QTimer m_schedTick;
    bool m_sleepBlocked = false;
    void schedulerTick();
};
