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
#include <gst/gst.h>

enum class ChannelState { Idle, Preview, Recording, Paused, Finalizing, Waiting, Reconnecting, Error };

// Ustawienia zapisu kanalu (zakladka Output).
struct OutputConfig {
    QString dir;                    // puste = folder domyslny
    QString nameTemplate;           // puste = FileNameTemplate::kDefault
    QString encoder = QStringLiteral("auto");
    int videoKbps = 8000;
    int audioKbps = 192;
    QString container = QStringLiteral("mkv");   // "mkv" albo "ts"
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
    bool operator==(const LiveConfig&) const = default;
};

// Jedna czesc nagrania (plik). Czasy liczone od poczatku nagrania.
struct RecordPart {
    QString file;
    qint64 startNs = 0;
    qint64 endNs = -1;
};

// Jedynym typem wejscia jest na razie generator testowy.
struct ChannelConfig {
    QString name;
    int pattern = 0;        // videotestsrc pattern
    int audioWave = 0;      // audiotestsrc wave: 0 sinus, 8 tiki, 4 cisza
    double audioVolume = 0.1;
    int width = 1280;
    int height = 720;
    int fps = 25;
    OutputConfig out;
    LiveConfig live;
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
    void setOutput(const OutputConfig& out);
    void setLive(const LiveConfig& live);    // dziala takze podczas nagrywania: galaz wyjscia jest dopinana w locie
    bool liveOn() const { return m_cfg.live.type != QLatin1String("off"); }
    bool liveFailed() const { return m_liveFailed; }
    QString liveText() const;                // krotki opis na kafel: "NDI", "SRT :9001"
    static QString srtUri(const QString& address);
    static QString defaultOutputDir();
    QString outputDir() const { return m_cfg.out.dir.isEmpty() ? defaultOutputDir() : m_cfg.out.dir; }

    ChannelState state() const { return m_state; }
    QString errorText() const { return m_error; }
    bool isSelected() const { return m_selected; }
    void setSelected(bool on);
    // "Nagrywa" = ma otwarty plik: takze w pauzie i w trakcie zamykania pliku.
    bool isRecording() const {
        return m_state == ChannelState::Recording || m_state == ChannelState::Paused || m_state == ChannelState::Finalizing;
    }
    bool canEdit() const { return !isRecording(); }

    const QImage& frame() const { return m_frame; }
    double level(int ch) const { return m_level[size_t(ch & 1)]; }   // 0..1 (-60..0 dBFS)
    QString formatText() const;
    QString sourceText() const;

    void startPreview();
    void stopPreview();
    // Duzy podglad (okno po dwukliku): klatki 1280x720 z pelna czestotliwoscia zamiast 480x270 / 15 kl./s.
    void setLargePreview(bool on);

    // Nagrywanie
    bool startRecording(const QString& session, int index);
    void stopRecording();
    void pauseRecording();           // plik zostaje otwarty, klatki z czasu pauzy nie trafiaja do pliku
    void resumeRecording();          // dalszy ciag w tym samym pliku, bez przerwy w znacznikach czasu
    void split();                    // zamknij biezacy plik i zacznij nastepny (na klatce kluczowej)
    int recordPart() const { return int(m_parts.size()); }
    qint64 recordMs() const;         // czas nagranego materialu (bez pauz)
    QString recordFile() const { return m_recFile; }
    qint64 recordBytes() const { return m_recBytes; }
    double recordMbps() const { return m_recMbps; }
    QString encoderName() const { return m_encoderName; }
    bool encoderIsHardware() const { return m_encoderHw; }
    void pollRecording();    // co 1 s: rozmiar plikow, biezacy bitrate, zapelnienie kolejki zapisu

signals:
    void frameChanged();
    void levelsChanged();
    void changed();          // nazwa, stan, zaznaczenie, konfiguracja, statystyki nagrania
    void configChanged();    // tylko ustawienia zapisywane w liscie kanalow
    void notice(const QString& text);
    void recordingFinished(const QString& file, bool clean);

private:
    static GstFlowReturn onSample(GstAppSink* sink, gpointer user);
    static GstBusSyncReply onBus(GstBus* bus, GstMessage* msg, gpointer user);
    static GstPadProbeReturn onRecVideo(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    static GstPadProbeReturn onRecAudio(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    GstPadProbeReturn recBuffer(GstPadProbeInfo* info, bool video);
    gint64 runningTimeNow() const;
    static GstPadProbeReturn onStopBlock(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    void setState(ChannelState s, const QString& error = {});
    void restart();
    QString liveDescription();
    void attachLive();
    void detachLive();
    void liveError(const QString& text);
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
    bool m_liveFailed = false;              // wyjscie na zywo zglosilo blad - wylaczone do zmiany ustawien
    std::atomic<int> m_gen{0};              // numer kolejnego pipeline'u (komunikaty ze starego sa ignorowane)
    GstElement* m_liveBin = nullptr;                    // watek GUI
    std::atomic<GstElement*> m_liveBinShared{nullptr};  // do porownan w watkach streamingu
    std::atomic<int> m_liveSerial{0};                   // numer kolejnego podpiecia wyjscia
    GstPad* m_liveTeeVideo = nullptr;
    GstPad* m_liveTeeAudio = nullptr;
    bool m_liveNoticed = false;             // komunikat o awarii biezacego podpiecia juz poszedl
    void applyPreviewSize();

    GstElement* m_recBin = nullptr;                    // watek GUI
    std::atomic<GstElement*> m_recBinShared{nullptr};  // do porownan w watkach streamingu
    std::atomic<gint64> m_recBase{0};                  // running-time poczatku nagrania
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
