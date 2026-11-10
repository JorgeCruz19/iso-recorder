#pragma once
#include <QImage>
#include <QObject>
#include <QString>
#include <array>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

enum class ChannelState { Idle, Preview, Recording, Paused, Waiting, Reconnecting, Error };

// Wersja 0.1: jedynym typem wejscia jest generator testowy.
struct ChannelConfig {
    QString name;
    int pattern = 0;        // videotestsrc pattern
    int audioWave = 0;      // audiotestsrc wave: 0 sinus, 8 tiki, 4 cisza
    double audioVolume = 0.1;
    int width = 1280;
    int height = 720;
    int fps = 25;
};

// Jeden kanal = konfiguracja + wlasny GstPipeline (awaria jednego nie dotyka pozostalych).
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

    ChannelState state() const { return m_state; }
    QString errorText() const { return m_error; }
    bool isSelected() const { return m_selected; }
    void setSelected(bool on);

    const QImage& frame() const { return m_frame; }
    double level(int ch) const { return m_level[size_t(ch & 1)]; }   // 0..1 (-60..0 dBFS)
    QString formatText() const;
    QString sourceText() const;

    void startPreview();
    void stopPreview();

signals:
    void frameChanged();
    void levelsChanged();
    void changed();          // nazwa, stan, zaznaczenie, konfiguracja

private:
    static GstFlowReturn onSample(GstAppSink* sink, gpointer user);
    static GstBusSyncReply onBus(GstBus* bus, GstMessage* msg, gpointer user);
    void setState(ChannelState s, const QString& error = {});
    void restart();

    ChannelConfig m_cfg;
    ChannelState m_state = ChannelState::Idle;
    QString m_error;
    bool m_selected = false;
    QImage m_frame;
    std::array<double, 2> m_level{0.0, 0.0};
    GstElement* m_pipeline = nullptr;
};
