#pragma once
#include <QDateTime>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <array>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include "Schedule.h"

enum class ChannelState { Idle, Preview, Recording, Paused, Finalizing, Waiting, Reconnecting, Error };

// Ustawienia zapisu kanalu (zakladka Output).
struct OutputConfig {
    QString dir;                    // puste = folder domyslny
    QString nameTemplate;           // puste = FileNameTemplate::kDefault
    // Druga lokalizacja zapisu: "off"; "spare" - folder zapasowy, do ktorego nagranie przechodzi (nowa czesc pliku), gdy na
    // dysku folderu glownego zostaje mniej niz spareMinMb; "mirror" - to samo nagranie rownoczesnie w obu folderach.
    QString dir2Mode = QStringLiteral("off");
    QString dir2;
    int spareMinMb = 5120;
    QString codec = QStringLiteral("h264");      // "h264", "h265", "av1", "prores" (EncoderFactory)
    QString encoder = QStringLiteral("auto");
    int videoKbps = 8000;
    int audioKbps = 192;
    QString container = QStringLiteral("mkv");   // "mkv", "ts", "mp4", "mov"
    int splitSeconds = 0;           // auto-split co N sekund (0 = wylaczony)
    qint64 splitBytes = 0;          // auto-split co N bajtow (0 = wylaczony)
    bool operator==(const OutputConfig&) const = default;
};

// Wyjscie na zywo: obraz i dzwiek kanalu ida rownolegle do nagrywania na NDI albo SRT (dziala takze bez nagrywania).
struct LiveConfig {
    QString type = QStringLiteral("off");   // "off", "ndi", "srt"
    QString ndiName;                        // puste = nazwa kanalu
    QString srtAddress;                     // ":9001" = ten komputer czeka na odbiorce; "host:9001" = laczy sie z odbiorca
    int srtKbps = 6000;
    QString when = QStringLiteral("always");  // "always" caly czas, "schedule" tylko w oknach harmonogramu nagrywania
    bool operator==(const LiveConfig&) const = default;
};

// Jedna czesc nagrania (plik). Czasy liczone od poczatku nagrania.
struct RecordPart {
    QString file;
    qint64 startNs = 0;
    qint64 endNs = -1;
};

// Zrodlo obrazu i dzwieku kanalu.
struct InputConfig {
    QString type = QStringLiteral("test");        // "test" generator, "screen" ekran, "window" okno, "camera" kamera, "decklink", "srt", "rtsp", "ndi", "none" wylaczone
    QString windowTitle;                          // okno: tytul (uchwyt jest szukany przy starcie kanalu)
    QString cameraMode;                           // kamera: "WxH" albo "WxH@licznik/mianownik" (klatkaz); puste = wybor sterownika
    QString lossPolicy = QStringLiteral("continue");   // zanik sygnalu sieciowego: "continue", "pause", "split"
    int gainDb = 0;                               // glosnosc wejscia dzwieku, dB
    QString uri;                                  // srt / rtsp: adres strumienia; ndi: nazwa zrodla "KOMPUTER (nazwa)"
    bool isNetwork() const { return type == QLatin1String("srt") || type == QLatin1String("rtsp") || type == QLatin1String("ndi"); }
    int screenIndex = 0;                          // ekran: numer monitora
    int deckIndex = 0;                            // DeckLink: numer wejscia karty (device-number)
    QString deckName;
    // dzwiek przychodzi razem z obrazem (strumien sieciowy, karta DeckLink) - pole "Audio source" nie ma zastosowania
    bool embeddedAudio() const { return isNetwork() || type == QLatin1String("decklink"); }
    QString cameraPath;                           // kamera: sciezka urzadzenia
    QString cameraName;
    QString audioType = QStringLiteral("test");   // "test" sygnal testowy, "none" cisza, "loopback" dzwiek systemowy, "device" wejscie
    QString audioDevice;                          // id urzadzenia WASAPI; puste = domyslne
    QString audioName;
    bool operator==(const InputConfig&) const = default;
};

struct ChannelConfig {
    QString name;
    int pattern = 0;        // videotestsrc pattern; 100 = zegar cyfrowy, 101 = zegar analogowy (rysowane przez program)
    int audioWave = 0;      // audiotestsrc wave: 0 sinus, 8 tiki, 4 cisza
    double audioVolume = 0.1;
    int width = 1280;
    int height = 720;
    int fps = 25;
    InputConfig in;
    OutputConfig out;
    LiveConfig live;
    ScheduleConfig sched;
};

// Jeden kanal = konfiguracja + wlasny GstPipeline (awaria jednego nie dotyka pozostalych).
// Galaz nagrywania jest dopinana do dzialajacego pipeline'u przy Rec i odpinana po Stop - podglad nie przerywa.
class Channel : public QObject {
    Q_OBJECT
public:
    explicit Channel(const ChannelConfig& cfg, QObject* parent = nullptr);
    ~Channel() override;

    const ChannelConfig& config() const { return m_cfg; }
    void setName(const QString& name);
    void setPattern(int pattern);
    void setAudioWave(int wave);
    void setFormat(int width, int height);
    void setFps(int fps);                    // klatkaz kanalu (podglad, nagranie, wyjscie)
    void reconnect();                        // zatrzymaj zrodlo i uruchom je od nowa (takze kanal w stanie bledu)
    void setInput(const InputConfig& in);
    void setOutput(const OutputConfig& out);
    void setLive(const LiveConfig& live);    // dziala takze podczas nagrywania: galaz wyjscia jest dopinana w locie
    bool liveOn() const { return m_cfg.live.type != QLatin1String("off"); }
    bool liveFailed() const { return m_liveFailed; }
    // Wyjscie faktycznie nadaje: wlaczone i (tryb "caly czas" albo trwa okno harmonogramu).
    bool liveActive() const { return liveOn() && (m_cfg.live.when != QLatin1String("schedule") || m_liveGate); }
    void setLiveGate(bool open);             // ustawia scheduler: trwa okno harmonogramu
    // Harmonogram: reguly mozna zmieniac takze podczas nagrywania. Status (najblizszy start, koniec biezacego okna)
    // wpisuje scheduler w ChannelManager - sluzy tylko do pokazania w interfejsie.
    void setSchedule(const ScheduleConfig& sched);
    void setScheduleStatus(const QDateTime& next, const QDateTime& until);
    QDateTime scheduleNext() const { return m_schedNext; }
    QDateTime scheduleUntil() const { return m_schedUntil; }
    bool waitingForSchedule() const { return m_state == ChannelState::Preview && m_schedNext.isValid(); }
    QString liveText() const;                // krotki opis na kafel: "NDI", "SRT :9001"
    static QString srtUri(const QString& address);
    static QString defaultOutputDir();
    // Liczba pipeline'ow porzuconych, bo zrodlo nie odpowiedzialo w limicie czasu (ich watki moga wisiec do konca procesu).
    static int abandonedPipelines();
    QString outputDir() const { return m_cfg.out.dir.isEmpty() ? defaultOutputDir() : m_cfg.out.dir; }
    // Foldery, do ktorych kanal pisze teraz (nagrywa) albo bedzie pisal (nie nagrywa): glowny lub zapasowy, plus lustrzany.
    QStringList activeDirs() const;
    QString recordingBaseDir() const { return m_recBaseDir.isEmpty() ? outputDir() : m_recBaseDir; }
    bool canSwitchToSpare() const;           // tryb "spare", nagrywa w folderze glownym, folder zapasowy podany
    void switchToSpare();                    // nastepna czesc pliku trafia do folderu zapasowego
    bool mirrorOn() const { return m_mirrorOn.load(); }
    bool mirrorFailed() const { return m_mirrorFailed.load(); }
    void breakMirrorForTest();               // test: nastepna czesc kopii lustrzanej dostaje niemozliwa sciezke

    ChannelState state() const { return m_state; }
    QString errorText() const { return m_error; }
    bool isSelected() const { return m_selected; }
    void setSelected(bool on);
    // "Nagrywa" = ma otwarty plik: takze w pauzie i w trakcie zamykania pliku.
    bool isRecording() const {
        return m_state == ChannelState::Recording || m_state == ChannelState::Paused || m_state == ChannelState::Finalizing;
    }
    bool canEdit() const { return !isRecording(); }
    // Zapis bez ponownego kodowania ("stream copy"): tylko SRT i RTSP; dla innych wejsc ustawienie dziala jak "auto".
    bool copyMode() const {
        return m_cfg.out.encoder == QLatin1String("copy") && (m_cfg.in.type == QLatin1String("srt") || m_cfg.in.type == QLatin1String("rtsp"));
    }
    bool inputOff() const { return m_cfg.in.type == QLatin1String("none"); }     // typ wejscia "None": kanal bez sygnalu

    const QImage& frame() const { return m_frame; }
    double level(int ch) const { return m_level[size_t(ch & 1)]; }   // 0..1 (-60..0 dBFS)
    QString formatText() const;
    QString sourceText() const;

    void startPreview();
    void stopPreview();
    // Duzy podglad (okno po dwukliku): klatki 1280x720 z pelna czestotliwoscia zamiast 480x270 / 15 kl./s.
    void setLargePreview(bool on);

    // Nagrywanie
    // Wspolny moment T dla wielu kanalow: wszystkie pipeline'y chodza na zegarze systemowym GStreamera, wiec jedna
    // wartosc clockNs (Channel::clockNow) oznacza te sama chwile w kazdym kanale. clockNs = 0 oznacza "teraz".
    static quint64 clockNow();
    // Start w dwoch krokach: startRecording dopina galaz zapisu (nic jeszcze nie trafia do pliku),
    // beginRecordingAt wyznacza chwile, od ktorej klatki sa zapisywane - te sama dla calej grupy.
    bool startRecording(const QString& session, int index);
    void beginRecordingAt(quint64 clockNs);
    void stopRecording(quint64 clockNs = 0);
    void pauseRecording(quint64 clockNs = 0);    // plik zostaje otwarty, klatki z czasu pauzy nie trafiaja do pliku
    void resumeRecording(quint64 clockNs = 0);   // dalszy ciag w tym samym pliku, bez przerwy w znacznikach czasu
    void split();                    // zamknij biezacy plik i zacznij nastepny (na klatce kluczowej)
    void splitToFolder(const QString& dir);      // jak split, ale nastepna czesc trafia do podanego folderu
    // Odsluch: dzwiek kanalu idzie dodatkowo na wskazana karte dzwiekowa (puste id = domyslne wyjscie systemu).
    void setMonitor(bool on, const QString& deviceId = {});
    bool monitorOn() const { return m_monitorPipe != nullptr; }
    static constexpr int ClockDigital = 100, ClockAnalog = 101;
    int recordPart() const { return int(m_parts.size()); }
    qint64 recordMs() const;         // czas nagranego materialu (bez pauz)
    QString recordFile() const { return m_recFile; }
    qint64 recordBytes() const { return m_recBytes; }
    double recordMbps() const { return m_recMbps; }
    QString encoderName() const { return m_encoderName; }
    bool encoderIsHardware() const { return m_encoderHw; }
    void pollRecording();    // co 1 s: rozmiar plikow, biezacy bitrate, zapelnienie kolejki zapisu
    void pollSource();       // co 1 s: stan zrodla sieciowego (zanik i powrot sygnalu)
    // Zrodlo sieciowe nie podaje sygnalu: kanal gra dalej (czarny obraz, cisza), nagranie trwa, zrodlo laczy sie ponownie.
    bool signalLost() const { return m_signalLost; }
    QString networkUri() const;
    GstElement* makeNdiSource() const;      // odbiornik NDI jako zrodlo dla fallbacksrc (pady "video" i "audio")

signals:
    void frameChanged();
    void levelsChanged();
    void changed();          // nazwa, stan, zaznaczenie, konfiguracja, statystyki nagrania
    void configChanged();    // tylko ustawienia zapisywane w liscie kanalow
    void autoRecordRequested();   // zrodlo wrocilo po awarii, a kanal przed nia nagrywal - wznow nagrywanie (nowy plik)
    void notice(const QString& text);
    void recordingFinished(const QString& file, bool clean);

private:
    static GstFlowReturn onSample(GstAppSink* sink, gpointer user);
    static GstBusSyncReply onBus(GstBus* bus, GstMessage* msg, gpointer user);
    static GstPadProbeReturn onRecVideo(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    static GstPadProbeReturn onRecAudio(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    static GstPadProbeReturn onRecVideoEvent(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    static GstPadProbeReturn onRecAudioEvent(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    GstPadProbeReturn recEvent(GstPadProbeInfo* info, bool video);
    GstPadProbeReturn recBuffer(GstPadProbeInfo* info, bool video);
    gint64 runningTimeNow() const;
    gint64 toRunning(quint64 clockNs) const;     // czas zegara -> running-time tego pipeline'u
    // --- kopia strumienia: zrodlo rozdzielone na strumien skompresowany (tee "cv"/"ca", do pliku) i rozpakowany (podglad)
    static void onCopyPad(GstElement* src, GstPad* pad, gpointer user);
    static GstPadProbeReturn onCopyCaps(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    void linkCopyPad(GstPad* pad, GstCaps* caps);
    static GstPadProbeReturn onCopyTap(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    void plugCopySilence();
    std::atomic<int> m_copyVideo{0};        // 0 brak, 1 H.264, 2 H.265
    std::atomic<int> m_copyAudio{0};        // 0 brak, 1 AAC, 2 inny obslugiwany, -1 nieobslugiwany (plik bez dzwieku)
    std::atomic<gint64> m_copyOffset{G_MININT64};   // running-time zegara minus running-time znacznikow zrodla
    std::atomic<qint64> m_copyLastMs{0};    // chwila ostatniej klatki ze zrodla (m_copyClock)
    std::atomic<qint64> m_copyFirstMs{-1};
    QElapsedTimer m_copyClock;
    bool m_copySilence = false;             // zrodlo bez dzwieku: do podgladu podlaczona cisza
    std::atomic<bool> m_recCopy{false};     // biezace nagranie jest kopia strumienia
    std::atomic<bool> m_copyNeedKey{false}; // zapis czeka na klatke kluczowa (poczatek)
    std::atomic<bool> m_copyStarted{false};
    QByteArray m_recTee[2];                 // nazwy tee, do ktorych dopieta jest galaz zapisu (obraz, dzwiek)
    QString m_recCodec = QStringLiteral("h264");
    QString m_recContainer = QStringLiteral("mkv");
    static GstPadProbeReturn onStopBlock(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    void setState(ChannelState s, const QString& error = {});
    void restart();
    void sourceFailed(const QString& text);     // awaria zrodla: kanal w stan bledu + ponawianie startu
    void scheduleRetry();
    void applyGain();
    void abandonPipeline();
    QString sourceDescription() const;      // poczatek opisu pipeline'u: zrodla do tee "tv" i "ta"
    QString liveDescription();
    void attachLive();
    void detachLive();
    void liveError(const QString& text);
    void pushClockFrame();                  // generator "zegar": jedna klatka do appsrc
    static gchar* onFormatLocation(GstElement* mux, guint fragment, gpointer user);
    void finishRecording(bool clean, const QString& reason = {});
    void writeSidecar(bool clean, const QString& reason) const;

    ChannelConfig m_cfg;
    ChannelState m_state = ChannelState::Idle;
    QString m_error;
    bool m_selected = false;
    QImage m_frame;
    std::array<double, 2> m_level{0.0, 0.0};
    GstElement* m_pipeline = nullptr;
    bool m_largePreview = false;
    bool m_wantPreview = false;             // kanal mial ruszyc, ale wejscie jest wylaczone - ruszy po zmianie typu wejscia
    bool m_signalLost = false;
    bool m_autoPaused = false;              // pauza wlaczona automatycznie przez zanik sygnalu
    QTimer m_retry;                         // ponawianie startu kanalu po awarii zrodla
    int m_retryCount = 0;
    bool m_autoRecord = false;              // po udanym restarcie wznowic nagrywanie
    QString m_pendingFail;                  // awaria zrodla czeka na zamkniecie pliku nagrania
    bool m_signalSeen = false;              // zrodlo sieciowe podalo sygnal przynajmniej raz od startu kanalu
    int m_recLosses = 0;                    // zaniki sygnalu w trakcie biezacego nagrania
    qint64 m_recLostMs = 0;
    QElapsedTimer m_lostClock;
    bool m_liveGate = false;                // trwa okno harmonogramu (dla wyjscia w trybie "schedule")
    QDateTime m_schedNext, m_schedUntil;
    bool m_liveFailed = false;              // wyjscie na zywo zglosilo blad - wylaczone do zmiany ustawien
    std::atomic<int> m_gen{0};              // numer kolejnego pipeline'u (komunikaty ze starego sa ignorowane)
    GstElement* m_liveBin = nullptr;                    // watek GUI
    std::atomic<GstElement*> m_liveBinShared{nullptr};  // do porownan w watkach streamingu
    std::atomic<int> m_liveSerial{0};                   // numer kolejnego podpiecia wyjscia
    GstPad* m_liveTeeVideo = nullptr;
    GstPad* m_liveTeeAudio = nullptr;
    // Odsluch to osobny, niezalezny pipeline (appsrc -> karta dzwiekowa), zasilany kopiami buforow z sondy na torze
    // dzwieku kanalu. Dzieki temu zadna awaria karty dzwiekowej nie moze zatrzymac kanalu ani nagrania.
    GstElement* m_monitorPipe = nullptr;                // watek GUI
    GstElement* m_monitorSrc = nullptr;                 // appsrc; uzywany w sondzie pod m_monMutex
    bool m_monCapsSet = false;
    std::mutex m_monMutex;
    std::atomic<int> m_monSerial{0};
    static GstPadProbeReturn onMonitorTap(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    static GstBusSyncReply onMonitorBus(GstBus* bus, GstMessage* msg, gpointer user);
    QString m_monitorDevice;
    QTimer m_clockTimer;                    // generator "zegar"
    QImage m_clockImage;
    std::mutex m_locMutex;                  // folder i wzorzec nazwy kolejnych czesci (czytane w watku streamingu)
    QString m_recDir, m_recPattern, m_recExt;
    QString m_recDir2;                      // zapis podwojny: folder biezacej czesci kopii (pod m_locMutex)
    QString m_recBaseDir, m_recBaseDir2;    // foldery bazowe nagrania (bez podfolderow "Split to new folder")
    std::atomic<int> m_partCounter2{0};
    bool m_onSpare = false;                 // nagranie przeszlo (albo zaczelo sie) w folderze zapasowym
    std::atomic<bool> m_mirrorOn{false};
    std::atomic<bool> m_mirrorFailed{false};
    std::atomic<GstElement*> m_mirrorMux{nullptr};
    static gchar* onFormatLocation2(GstElement* mux, guint fragment, gpointer user);
    void mirrorError(const QString& text);
    static GstPadProbeReturn onMirrorGate(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    bool m_mirrorNoticed = false;           // komunikat o awarii kopii juz poszedl
    std::atomic<int> m_partCounter{0};
    bool m_liveNoticed = false;             // komunikat o awarii biezacego podpiecia juz poszedl
    void applyPreviewSize();

    GstElement* m_recBin = nullptr;                    // watek GUI
    std::atomic<GstElement*> m_recBinShared{nullptr};  // do porownan w watkach streamingu
    std::atomic<gint64> m_recBase{0};                  // running-time poczatku nagrania
    std::atomic<gint64> m_stopRt{G_MAXINT64};          // running-time konca nagrania (bufory od tej chwili odpadaja)
    std::atomic<gint64> m_firstVideoRt{-1};            // pierwsza i ostatnia klatka, ktora trafila do pliku
    std::atomic<gint64> m_lastVideoRt{-1};
    quint64 m_recStartClock = 0;                       // wspolna chwila startu (czas zegara)
    // Segmenty zrodla na wejsciu galezi zapisu (osobno obraz i dzwiek). Czytane i pisane tylko w watku streamingu
    // danego pada: zdarzenia i bufory jednego pada ida tym samym watkiem.
    GstSegment m_recSeg[2];
    bool m_recHaveSeg[2] = {false, false};
    std::atomic<bool> m_stopSeenVideo{false};          // obraz / dzwiek doszly juz do chwili Stop
    std::atomic<bool> m_stopSeenAudio{false};
    QTimer m_stopPoll;
    QElapsedTimer m_stopWait;
    qint64 m_stopWaitMs = 600;
    void closeBranch();
    // Pauza (instrukcja_koncowa.md, 3.1). Wszystkie czasy w running-time pipeline'u, jeden muteks dla obrazu i dzwieku.
    // Granice pauzy wyznacza klatka wideo: pierwsza odrzucona (startRt) i pierwsza przepuszczona po wznowieniu (resumeRt).
    struct PauseState {
        enum Phase { Run, PausePending, Paused, ResumePending };
        std::mutex mutex;
        Phase phase = Run;
        gint64 reqRt = 0;          // chwila klikniecia Pause / Resume
        gint64 startRt = 0;        // pierwsza odrzucona klatka wideo
        gint64 resumeRt = 0;       // pierwsza klatka wideo po ostatnim wznowieniu
        gint64 total = 0;          // suma pauz - odejmowana od znacznikow obrazu i dzwieku
        bool audioDiscont = false;
        std::vector<std::pair<gint64, gint64>> pauses;   // (polozenie w pliku, dlugosc) w ns
    } m_pause;
    qint64 m_pausedMs = 0;          // watek GUI: suma zakonczonych pauz (do licznika czasu)
    QElapsedTimer m_pauseClock;
    GstPad* m_teeVideo = nullptr;
    GstPad* m_teeAudio = nullptr;
    QString m_recFile;
    QString m_recSidecar;
    QString m_recSession;
    int m_recIndex = 0;
    QDateTime m_recStartUtc;
    QList<RecordPart> m_parts;
    qint64 m_closedBytes = 0;       // suma rozmiarow zamknietych czesci
    int m_slowTicks = 0;            // ile sekund z rzedu kolejka zapisu jest zapelniona ponad polowe
    bool m_slowWarned = false;
    int m_fkuCount = 0;
    QElapsedTimer m_recClock;
    qint64 m_recBytes = 0;
    qint64 m_recBytesPrev = 0;
    double m_recMbps = 0;
    QString m_encoderName;
    bool m_encoderHw = false;
    QTimer m_finalizeTimeout;
};
