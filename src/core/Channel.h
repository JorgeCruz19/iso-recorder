#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>
#include <array>
#include <atomic>

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
    static QString defaultOutputDir();
    QString outputDir() const { return m_cfg.out.dir.isEmpty() ? defaultOutputDir() : m_cfg.out.dir; }

    ChannelState state() const { return m_state; }
    QString errorText() const { return m_error; }
    bool isSelected() const { return m_selected; }
    void setSelected(bool on);
    bool isRecording() const { return m_state == ChannelState::Recording || m_state == ChannelState::Finalizing; }
    bool canEdit() const { return !isRecording(); }

    const QImage& frame() const { return m_frame; }
    double level(int ch) const { return m_level[size_t(ch & 1)]; }   // 0..1 (-60..0 dBFS)
    QString formatText() const;
    QString sourceText() const;

    void startPreview();
    void stopPreview();

    // Nagrywanie
    bool startRecording(const QString& session, int index);
    void stopRecording();
    qint64 recordMs() const { return isRecording() ? m_recClock.elapsed() : 0; }
    QString recordFile() const { return m_recFile; }
    qint64 recordBytes() const { return m_recBytes; }
    double recordMbps() const { return m_recMbps; }
    QString encoderName() const { return m_encoderName; }
    bool encoderIsHardware() const { return m_encoderHw; }
    void pollRecording();    // co 1 s: rozmiar pliku i biezacy bitrate

signals:
    void frameChanged();
    void levelsChanged();
    void changed();          // nazwa, stan, zaznaczenie, konfiguracja, statystyki nagrania
    void notice(const QString& text);
    void recordingFinished(const QString& file, bool clean);

private:
    static GstFlowReturn onSample(GstAppSink* sink, gpointer user);
    static GstBusSyncReply onBus(GstBus* bus, GstMessage* msg, gpointer user);
    static GstPadProbeReturn onRecBuffer(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    static GstPadProbeReturn onStopBlock(GstPad* pad, GstPadProbeInfo* info, gpointer user);
    void setState(ChannelState s, const QString& error = {});
    void restart();
    void finishRecording(bool clean, const QString& reason = {});

    ChannelConfig m_cfg;
    ChannelState m_state = ChannelState::Idle;
    QString m_error;
    bool m_selected = false;
    QImage m_frame;
    std::array<double, 2> m_level{0.0, 0.0};
    GstElement* m_pipeline = nullptr;

    GstElement* m_recBin = nullptr;                    // watek GUI
    std::atomic<GstElement*> m_recBinShared{nullptr};  // do porownan w watkach streamingu
    std::atomic<gint64> m_recBase{0};                  // running-time poczatku nagrania
    GstPad* m_teeVideo = nullptr;
    GstPad* m_teeAudio = nullptr;
    QString m_recFile;
    QElapsedTimer m_recClock;
    qint64 m_recBytes = 0;
    qint64 m_recBytesPrev = 0;
    double m_recMbps = 0;
    QString m_encoderName;
    bool m_encoderHw = false;
    QTimer m_finalizeTimeout;
};
