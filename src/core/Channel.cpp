#include "Channel.h"

#include "Lang.h"
#include "media/EncoderFactory.h"
#include "util/FileNameTemplate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <gst/video/video.h>
#include <gst/video/video-event.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace {
std::atomic<int> g_abandoned{0};

// Zmiana stanu z limitem czasu. Sterowniki urzadzen (np. wirtualna kamera bez zrodla) potrafia zablokowac zmiane stanu
// na zawsze; wykonana wprost w watku okna zawiesilaby caly program. Zmiana idzie wiec w watku pomocniczym, a watek okna
// czeka najwyzej timeoutMs. Zwraca false, gdy zmiana nie skonczyla sie w limicie (trwa dalej w tle).
bool setStateTimed(GstElement* element, GstState state, int timeoutMs, GstStateChangeReturn* result = nullptr) {
    struct Job {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        GstState state = GST_STATE_NULL;
        GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;
    };
    auto job = std::make_shared<Job>();
    job->state = state;
    gst_element_call_async(
        element,
        [](GstElement* el, gpointer data) {
            const std::shared_ptr<Job> j = *static_cast<std::shared_ptr<Job>*>(data);
            const GstStateChangeReturn r = gst_element_set_state(el, j->state);
            {
                std::lock_guard<std::mutex> lock(j->mutex);
                j->result = r;
                j->done = true;
            }
            j->cv.notify_all();
        },
        new std::shared_ptr<Job>(job), [](gpointer data) { delete static_cast<std::shared_ptr<Job>*>(data); });
    std::unique_lock<std::mutex> lock(job->mutex);
    const bool finished = job->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return job->done; });
    if (finished && result) *result = job->result;
    return finished;
}
}  // namespace

int Channel::abandonedPipelines() { return g_abandoned.load(); }

// Pipeline, ktory nie odpowiada: odcinamy go od kanalu (szyna, podglad) i zostawiamy. Jego watki moga wisiec dalej,
// ale nie maja juz drogi do obiektu kanalu; proces konczy sie wtedy bez czekania na nie.
void Channel::abandonPipeline() {
    if (!m_pipeline) return;
    ++g_abandoned;
    if (GstElement* sink = gst_bin_get_by_name(GST_BIN(m_pipeline), "preview")) {
        GstAppSinkCallbacks none{};
        gst_app_sink_set_callbacks(GST_APP_SINK(sink), &none, nullptr, nullptr);
        gst_object_unref(sink);
    }
    GstBus* bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    gst_object_unref(bus);
    ++m_gen;                            // komunikaty, ktore zdazyly wejsc do kolejki, zostana pominiete
    gst_object_unref(m_pipeline);       // watek pomocniczy trzyma wlasna referencje do konca swojej pracy
    m_pipeline = nullptr;
    m_level = {0.0, 0.0};
}

Channel::Channel(const ChannelConfig& cfg, QObject* parent) : QObject(parent), m_cfg(cfg) {
    m_finalizeTimeout.setSingleShot(true);
    m_finalizeTimeout.setInterval(10000);
    connect(&m_finalizeTimeout, &QTimer::timeout, this, [this] { finishRecording(false, T("finalization timed out")); });
    m_stopPoll.setInterval(15);
    connect(&m_stopPoll, &QTimer::timeout, this, [this] {
        if ((m_stopSeenVideo && m_stopSeenAudio) || m_stopWait.elapsed() > m_stopWaitMs) closeBranch();
    });
}

Channel::~Channel() { stopPreview(); }

void Channel::setName(const QString& name) {
    if (name == m_cfg.name || name.trimmed().isEmpty()) return;
    m_cfg.name = name.trimmed();
    emit changed();
    emit configChanged();
}

void Channel::setPattern(int pattern) {
    if (pattern == m_cfg.pattern || !canEdit()) return;
    m_cfg.pattern = pattern;
    restart();
    emit configChanged();
}

void Channel::setAudioWave(int wave) {
    if (wave == m_cfg.audioWave || !canEdit()) return;
    m_cfg.audioWave = wave;
    restart();
    emit configChanged();
}

void Channel::setFormat(int width, int height) {
    if ((width == m_cfg.width && height == m_cfg.height) || !canEdit()) return;
    m_cfg.width = width;
    m_cfg.height = height;
    restart();
    emit configChanged();
}

void Channel::setInput(const InputConfig& in) {
    if (in == m_cfg.in || !canEdit()) return;
    m_cfg.in = in;
    restart();
    emit configChanged();
}

void Channel::setOutput(const OutputConfig& out) {
    if (!canEdit() || out == m_cfg.out) return;
    m_cfg.out = out;
    emit changed();
    emit configChanged();
}

// Wyjscie mozna wlaczyc, wylaczyc i zmienic w kazdej chwili, takze podczas nagrywania: galaz jest dopinana
// do dzialajacego pipeline'u tak samo jak galaz nagrywania i nie dotyka ani zrodla, ani pliku.
void Channel::setLive(const LiveConfig& live) {
    if (live == m_cfg.live && !m_liveFailed) return;
    m_cfg.live = live;
    m_liveFailed = false;
    if (m_pipeline) {
        detachLive();
        attachLive();
    }
    emit changed();
    emit configChanged();
}

// Adres z pola ustawien -> URI dla srtsink. Sam port = tryb listener (odbiorca laczy sie z tym komputerem).
QString Channel::srtUri(const QString& address) {
    QString a = address.trimmed();
    if (a.startsWith(QLatin1String("srt://"), Qt::CaseInsensitive)) a = a.mid(6);
    bool digits = !a.isEmpty();
    for (QChar c : a) digits = digits && c.isDigit();
    if (digits) a.prepend(QLatin1Char(':'));
    if (a.isEmpty()) a = QStringLiteral(":9000");
    if (!a.contains(QLatin1Char('?'))) a += a.startsWith(QLatin1Char(':')) ? QStringLiteral("?mode=listener") : QStringLiteral("?mode=caller");
    return QStringLiteral("srt://") + a;
}

QString Channel::liveText() const {
    if (m_cfg.live.type == QLatin1String("ndi")) return QStringLiteral("NDI");
    if (m_cfg.live.type == QLatin1String("srt")) {
        QString a = srtUri(m_cfg.live.srtAddress).mid(6);
        return QStringLiteral("SRT ") + a.left(a.indexOf(QLatin1Char('?')));
    }
    return {};
}

// Opis galezi wyjscia (osobny bin). errorignore na wejsciu sprawia, ze awaria wyjscia w trakcie pracy
// nie zatrzymuje zrodla, podgladu ani nagrywania.
QString Channel::liveDescription() {
    auto clean = [](QString s) { return s.remove(QLatin1Char('"')).remove(QLatin1Char('\\')).trimmed(); };
    if (m_cfg.live.type == QLatin1String("ndi")) {
        QString name = clean(m_cfg.live.ndiName);
        if (name.isEmpty()) name = clean(m_cfg.name);
        return QStringLiteral(
            "errorignore name=lv_in ! queue name=out_vq leaky=downstream max-size-buffers=5 ! videoconvert ! video/x-raw,format=UYVY "
            "! ndisinkcombiner name=out_comb ! ndisink name=out_sink ndi-name=\"%1\" "
            "errorignore name=la_in ! queue name=out_aq leaky=downstream ! audioconvert ! audio/x-raw,format=F32LE ! out_comb.audio").arg(name);
    }
    const EncoderFactory::Choice enc = EncoderFactory::pick(QStringLiteral("auto"), m_cfg.width, m_cfg.height, m_cfg.fps, m_cfg.live.srtKbps);
    if (enc.name.isEmpty()) return {};
    return QStringLiteral(
        "errorignore name=lv_in ! queue name=out_vq leaky=downstream max-size-buffers=5 ! %1 ! h264parse config-interval=-1 "
        "! mpegtsmux name=out_mux alignment=7 ! srtsink name=out_sink uri=\"%2\" wait-for-connection=false sync=false "
        "errorignore name=la_in ! queue name=out_aq leaky=downstream ! audioconvert ! audioresample ! avenc_aac bitrate=128000 ! aacparse ! out_mux.")
        .arg(enc.launch, clean(srtUri(m_cfg.live.srtAddress)));
}

void Channel::attachLive() {
    if (!m_pipeline || m_liveBin || !liveOn() || m_liveFailed) return;
    ++m_liveSerial;
    m_liveNoticed = false;
    const QString desc = liveDescription();
    GError* err = nullptr;
    GstElement* bin = desc.isEmpty() ? nullptr : gst_parse_bin_from_description(desc.toUtf8().constData(), FALSE, &err);
    if (!bin || err) {
        const QString text = err ? QString::fromUtf8(err->message) : T("no working H.264 encoder found");
        g_clear_error(&err);
        if (bin) gst_object_unref(bin);
        liveError(text);
        return;
    }
    GstElement* vin = gst_bin_get_by_name(GST_BIN(bin), "lv_in");
    GstElement* ain = gst_bin_get_by_name(GST_BIN(bin), "la_in");
    GstPad* vs = gst_element_get_static_pad(vin, "sink");
    GstPad* as = gst_element_get_static_pad(ain, "sink");
    GstPad* ghostV = gst_ghost_pad_new("video_sink", vs);
    GstPad* ghostA = gst_ghost_pad_new("audio_sink", as);
    gst_element_add_pad(bin, ghostV);
    gst_element_add_pad(bin, ghostA);
    gst_object_unref(vs);
    gst_object_unref(as);
    gst_object_unref(vin);
    gst_object_unref(ain);

    gst_object_ref(bin);                       // wlasna referencja na czas dzialania wyjscia
    gst_bin_add(GST_BIN(m_pipeline), bin);
    m_liveBin = bin;
    m_liveBinShared = bin;
    // Najpierw stan, potem podpiecie do tee. Zajety port albo brak srodowiska NDI wychodzi juz tutaj.
    const bool started = gst_element_sync_state_with_parent(bin);
    GstElement* tv = gst_bin_get_by_name(GST_BIN(m_pipeline), "tv");
    GstElement* ta = gst_bin_get_by_name(GST_BIN(m_pipeline), "ta");
    bool ok = started;
    if (ok) {
        m_liveTeeVideo = gst_element_request_pad_simple(tv, "src_%u");
        m_liveTeeAudio = gst_element_request_pad_simple(ta, "src_%u");
        ok = m_liveTeeVideo && m_liveTeeAudio && gst_pad_link(m_liveTeeVideo, ghostV) == GST_PAD_LINK_OK &&
             gst_pad_link(m_liveTeeAudio, ghostA) == GST_PAD_LINK_OK;
    }
    gst_object_unref(tv);
    gst_object_unref(ta);
    if (!ok) {
        // Elementy zwykle zglaszaja wlasny, dokladniejszy komunikat na szynie; ten jest zapasowy.
        const int serial = m_liveSerial.load();
        m_liveFailed = true;
        detachLive();
        QTimer::singleShot(400, this, [this, serial] {
            if (serial == m_liveSerial.load() && m_liveFailed && !m_liveNoticed) liveError(T("cannot start the output"));
        });
        emit changed();
    }
}

// Odpiecie galezi wyjscia od dzialajacego pipeline'u. Kolejnosc ma znaczenie: m_liveBinShared jest zerowany dopiero
// po zatrzymaniu galezi, zeby jej ostatnie komunikaty byly jeszcze rozpoznane jako komunikaty wyjscia.
void Channel::detachLive() {
    if (!m_liveBin) return;
    GstElement* bin = m_liveBin;
    m_liveBin = nullptr;
    GstElement* tv = m_pipeline ? gst_bin_get_by_name(GST_BIN(m_pipeline), "tv") : nullptr;
    GstElement* ta = m_pipeline ? gst_bin_get_by_name(GST_BIN(m_pipeline), "ta") : nullptr;
    for (auto [tee, pad] : {std::pair{tv, &m_liveTeeVideo}, std::pair{ta, &m_liveTeeAudio}}) {
        if (*pad) {
            if (GstPad* peer = gst_pad_get_peer(*pad)) {
                gst_pad_unlink(*pad, peer);
                gst_object_unref(peer);
            }
            if (tee) gst_element_release_request_pad(tee, *pad);
            gst_object_unref(*pad);
            *pad = nullptr;
        }
        if (tee) gst_object_unref(tee);
    }
    gst_element_set_state(bin, GST_STATE_NULL);
    m_liveBinShared = nullptr;
    if (m_pipeline) gst_bin_remove(GST_BIN(m_pipeline), bin);
    gst_object_unref(bin);
}

// Awaria wyjscia: wyjscie jest wylaczane do zmiany ustawien, kanal i nagranie dzialaja dalej.
void Channel::liveError(const QString& text) {
    m_liveFailed = true;
    detachLive();
    if (!m_liveNoticed) {
        m_liveNoticed = true;
        emit notice(T("%1: live output stopped — %2").arg(m_cfg.name, text));
    }
    emit changed();
}

QString Channel::defaultOutputDir() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).filePath(QStringLiteral("ISO Recorder"));
}

void Channel::setSelected(bool on) {
    if (on == m_selected) return;
    m_selected = on;
    emit changed();
}

QString Channel::formatText() const {
    if (m_cfg.width * 9 == m_cfg.height * 16) return QStringLiteral("%1p%2").arg(m_cfg.height).arg(m_cfg.fps);
    return QStringLiteral("%1×%2p%3").arg(m_cfg.width).arg(m_cfg.height).arg(m_cfg.fps);
}

// Adres z pola ustawien -> URI zrodla. SRT: sam port = ten komputer czeka na nadawce (listener).
QString Channel::networkUri() const {
    const QString u = m_cfg.in.uri.trimmed();
    if (m_cfg.in.type == QLatin1String("srt")) return srtUri(u);
    return u.contains(QLatin1String("://")) ? u : QStringLiteral("rtsp://") + u;
}

QString Channel::sourceText() const {
    if (m_cfg.in.isNetwork()) {
        QString a = networkUri();
        a = a.mid(a.indexOf(QLatin1String("://")) + 3);
        if (a.contains(QLatin1Char('@'))) a = a.mid(a.indexOf(QLatin1Char('@')) + 1);     // bez loginu i hasla
        if (a.contains(QLatin1Char('?'))) a = a.left(a.indexOf(QLatin1Char('?')));
        return (m_cfg.in.type == QLatin1String("srt") ? QStringLiteral("SRT ") : QStringLiteral("RTSP ")) + a;
    }
    if (m_cfg.in.type == QLatin1String("screen")) return T("Screen %1").arg(m_cfg.in.screenIndex + 1);
    if (m_cfg.in.type == QLatin1String("camera")) return m_cfg.in.cameraName.isEmpty() ? T("Camera") : m_cfg.in.cameraName;
    return T("Test generator");
}

void Channel::setState(ChannelState s, const QString& error) {
    m_state = s;
    m_error = error;
    emit changed();
}

void Channel::restart() {
    const bool was = m_pipeline != nullptr;
    stopPreview();
    if (was) startPreview();
    emit changed();
}

// Zrodla kanalu. Kazde zrodlo obrazu konczy sie tym samym formatem (rozmiar i klatkaz z ustawien kanalu, kwadratowy
// piksel, czarne pasy przy innych proporcjach), a dzwieku - stereo 48 kHz. Dalsza czesc pipeline'u jest wspolna.
QString Channel::sourceDescription() const {
    auto quoted = [](QString s) {      // wartosc w cudzyslowie w skladni gst_parse_launch
        return QLatin1Char('"') + s.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\"")) + QLatin1Char('"');
    };
    const QString size = QStringLiteral("width=%1,height=%2,pixel-aspect-ratio=1/1").arg(m_cfg.width).arg(m_cfg.height);
    if (m_cfg.in.isNetwork()) {
        // fallbacksrc sam dekoduje strumien, a przy zaniku podaje czarny obraz i cisze i laczy sie ponownie -
        // dalsza czesc pipeline'u (takze nagrywanie) nie widzi przerwy.
        return QStringLiteral("fallbacksrc name=fs uri=%1 timeout=2000000000 restart-timeout=3000000000 retry-timeout=86400000000000 "
                              "enable-dummy=true immediate-fallback=true "
                              "fs. ! queue ! videoconvertscale add-borders=true ! videorate ! video/x-raw,%2,framerate=%3/1 ! tee name=tv "
                              "fs. ! queue ! audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=2 ")
            .arg(quoted(networkUri()), size).arg(m_cfg.fps);
    }
    QString video;
    if (m_cfg.in.type == QLatin1String("screen")) {
        video = QStringLiteral("d3d11screencapturesrc monitor-index=%1 show-cursor=true ! capsfilter caps=\"video/x-raw(memory:D3D11Memory),framerate=%2/1\" "
                               "! d3d11download ! videoconvertscale add-borders=true ! video/x-raw,%3")
                    .arg(m_cfg.in.screenIndex).arg(m_cfg.fps).arg(size);
    } else if (m_cfg.in.type == QLatin1String("camera")) {
        video = QStringLiteral("mfvideosrc %1 ! decodebin ! videoconvertscale add-borders=true ! videorate ! video/x-raw,%2,framerate=%3/1")
                    .arg(m_cfg.in.cameraPath.isEmpty() ? QString() : QStringLiteral("device-path=") + quoted(m_cfg.in.cameraPath))
                    .arg(size).arg(m_cfg.fps);
    } else {
        video = QStringLiteral("videotestsrc is-live=true pattern=%1 ! video/x-raw,%2,framerate=%3/1").arg(m_cfg.pattern).arg(size).arg(m_cfg.fps);
    }
    QString audio;
    if (m_cfg.in.audioType == QLatin1String("loopback") || m_cfg.in.audioType == QLatin1String("device")) {
        audio = QStringLiteral("wasapi2src %1 %2 ! audioconvert ! audioresample")
                    .arg(m_cfg.in.audioType == QLatin1String("loopback") ? QStringLiteral("loopback=true") : QString())
                    .arg(m_cfg.in.audioDevice.isEmpty() ? QString() : QStringLiteral("device=") + quoted(m_cfg.in.audioDevice));
    } else {
        const bool silent = m_cfg.in.audioType == QLatin1String("none");
        audio = QStringLiteral("audiotestsrc is-live=true wave=%1 volume=%2")
                    .arg(silent ? 4 : m_cfg.audioWave).arg(QString::number(m_cfg.audioVolume, 'f', 3));
    }
    return video + QStringLiteral(" ! tee name=tv ") + audio + QStringLiteral(" ! audio/x-raw,rate=48000,channels=2 ");
}

void Channel::startPreview() {
    if (m_pipeline) return;
    // tee "tv"/"ta" sa punktami zaczepienia galezi nagrywania.
    const QString desc = sourceDescription() + QStringLiteral(
        "! level name=level interval=100000000 post-messages=true ! tee name=ta "
        "ta. ! queue ! fakesink sync=false "
        "tv. ! queue leaky=downstream max-size-buffers=2 ! videorate name=pvrate drop-only=true max-rate=15 "
        // staly kadr 16:9 i kwadratowy piksel: videoconvertscale sam dopisuje czarne pasy dla innych proporcji
        "! videoconvertscale add-borders=true "
        "! capsfilter name=pvcaps caps=video/x-raw,format=BGRx,width=480,height=270,pixel-aspect-ratio=1/1 "
        "! appsink name=preview sync=false max-buffers=1 drop=true");
    ++m_gen;

    GError* err = nullptr;
    m_pipeline = gst_parse_launch(desc.toUtf8().constData(), &err);
    if (!m_pipeline || err) {
        const QString text = err ? QString::fromUtf8(err->message) : T("cannot build the pipeline");
        g_clear_error(&err);
        if (m_pipeline) { gst_object_unref(m_pipeline); m_pipeline = nullptr; }
        setState(ChannelState::Error, text);
        return;
    }
    // EOS galezi nagrywania nie moze czekac na EOS calego pipeline'u (podglad gra dalej) -
    // pipeline przekazuje go od razu jako komunikat "GstBinForwarded".
    g_object_set(m_pipeline, "message-forward", TRUE, nullptr);
    // Jeden zegar dla wszystkich kanalow (nie zegar urzadzenia audio) - warunek wspolnego startu, pauzy i stopu grupy.
    GstClock* sys = gst_system_clock_obtain();
    gst_pipeline_use_clock(GST_PIPELINE(m_pipeline), sys);
    gst_object_unref(sys);
    applyPreviewSize();

    GstElement* sink = gst_bin_get_by_name(GST_BIN(m_pipeline), "preview");
    GstAppSinkCallbacks cb{};
    cb.new_sample = &Channel::onSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &cb, this, nullptr);
    gst_object_unref(sink);

    GstBus* bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, &Channel::onBus, this, nullptr);
    gst_object_unref(bus);

    GstStateChangeReturn started = GST_STATE_CHANGE_FAILURE;
    if (!setStateTimed(m_pipeline, GST_STATE_PLAYING, 2500, &started)) {
        // zrodlo nie odpowiada (np. sterownik kamery czeka na sygnal, ktorego nie ma)
        abandonPipeline();
        setState(ChannelState::Error, T("the source does not respond"));
        emit notice(T("%1: the source does not respond").arg(m_cfg.name));
        return;
    }
    if (started == GST_STATE_CHANGE_FAILURE) {
        stopPreview();
        setState(ChannelState::Error, T("cannot start the preview"));
        return;
    }
    m_signalLost = false;
    m_signalSeen = false;
    setState(ChannelState::Preview);
    attachLive();
}

void Channel::setLargePreview(bool on) {
    if (m_largePreview == on) return;
    m_largePreview = on;
    applyPreviewSize();
}

// Rozmiar i czestotliwosc klatek podgladu mozna zmieniac w dzialajacym pipeline'ie - skaler renegocjuje format.
void Channel::applyPreviewSize() {
    if (!m_pipeline) return;
    GstElement* caps = gst_bin_get_by_name(GST_BIN(m_pipeline), "pvcaps");
    GstElement* rate = gst_bin_get_by_name(GST_BIN(m_pipeline), "pvrate");
    if (caps) {
        GstCaps* c = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRx",
                                         "width", G_TYPE_INT, m_largePreview ? 1280 : 480,
                                         "height", G_TYPE_INT, m_largePreview ? 720 : 270,
                                         "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, nullptr);
        g_object_set(caps, "caps", c, nullptr);
        gst_caps_unref(c);
        gst_object_unref(caps);
    }
    if (rate) {
        g_object_set(rate, "max-rate", m_largePreview ? 60 : 15, nullptr);
        gst_object_unref(rate);
    }
}

void Channel::stopPreview() {
    if (!m_pipeline) return;
    if (m_recBin) finishRecording(false, T("channel stopped"));   // wymuszone zamkniecie, bez czekania na EOS
    detachLive();
    // NULL zatrzymuje watki streamingu - po tym zaden callback juz nie wejdzie. Zrodlo, ktore nie odpowiada, mogloby
    // zablokowac zatrzymanie na zawsze, wiec czekamy najwyzej 2 s i w razie czego porzucamy pipeline.
    if (!setStateTimed(m_pipeline, GST_STATE_NULL, 2000)) {
        emit notice(T("%1: the source does not respond").arg(m_cfg.name));
        abandonPipeline();
        m_state = ChannelState::Idle;
        return;
    }
    GstBus* bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    gst_object_unref(bus);
    gst_object_unref(m_pipeline);
    m_pipeline = nullptr;
    m_level = {0.0, 0.0};
    m_state = ChannelState::Idle;
}

// ---------------------------------------------------------------- nagrywanie

bool Channel::startRecording(const QString& session, int index) {
    if (m_state != ChannelState::Preview || m_recBin || !m_pipeline) return false;

    const EncoderFactory::Choice enc =
        EncoderFactory::pick(m_cfg.out.encoder, m_cfg.width, m_cfg.height, m_cfg.fps, m_cfg.out.videoKbps);
    if (enc.name.isEmpty()) {
        emit notice(T("%1: no working H.264 encoder found").arg(m_cfg.name));
        return false;
    }
    if (m_cfg.out.encoder != QLatin1String("auto") && m_cfg.out.encoder != enc.name)
        emit notice(T("%1: encoder %2 is not available, using %3")
                        .arg(m_cfg.name, EncoderFactory::displayName(m_cfg.out.encoder), EncoderFactory::displayName(enc.name)));

    const QString dir = outputDir();
    if (!QDir().mkpath(dir) || !QFileInfo(dir).isWritable()) {
        emit notice(T("%1: cannot write to folder %2").arg(m_cfg.name, QDir::toNativeSeparators(dir)));
        return false;
    }
    const bool ts = m_cfg.out.container == QLatin1String("ts");
    const QString ext = ts ? QStringLiteral(".ts") : QStringLiteral(".mkv");
    FileNameTemplate::Vars vars{session, m_cfg.name, sourceText(), index, QDateTime::currentDateTime()};
    QString pattern = FileNameTemplate::expand(m_cfg.out.nameTemplate, vars);
    for (int n = 2; QFileInfo::exists(QDir(dir).filePath(FileNameTemplate::withPart(pattern, 1) + ext)); ++n)
        pattern = FileNameTemplate::expand(m_cfg.out.nameTemplate, vars) + QStringLiteral("_%1").arg(n);   // kolizja nazw
    const QString location = QDir(dir).filePath(pattern + ext);

    // Kolejki galezi nagrywania nigdy nie gubia danych (leaky tylko w podgladzie).
    const QString desc = QStringLiteral(
        "queue name=vq max-size-time=3000000000 max-size-buffers=0 max-size-bytes=0 ! %1 ! h264parse name=vp "
        "queue name=aq max-size-time=3000000000 max-size-buffers=0 max-size-bytes=0 ! audioconvert ! audioresample "
        "! avenc_aac bitrate=%2 ! aacparse name=ap")
        .arg(enc.launch).arg(m_cfg.out.audioKbps * 1000);
    GError* err = nullptr;
    GstElement* bin = gst_parse_bin_from_description(desc.toUtf8().constData(), FALSE, &err);
    if (!bin || err) {
        emit notice(QStringLiteral("%1: %2").arg(m_cfg.name, err ? QString::fromUtf8(err->message) : T("cannot build the pipeline")));
        g_clear_error(&err);
        if (bin) gst_object_unref(bin);
        return false;
    }
    // splitmuxsink: muxer trzeba podac PRZED zadaniem padow; async-finalize=false -> EOS oznacza gotowy plik.
    GstElement* mux = gst_element_factory_make("splitmuxsink", "mux");
    GstElement* mkv = gst_element_factory_make(ts ? "mpegtsmux" : "matroskamux", nullptr);
    if (!mux || !mkv) {
        emit notice(T("%1: cannot build the pipeline").arg(m_cfg.name));
        if (mux) gst_object_unref(mux);
        if (mkv) gst_object_unref(mkv);
        gst_object_unref(bin);
        return false;
    }
    // Kazda czesc MKV zaczyna sie od zera (wygodniej w montazu); polozenie czesci na osi nagrania jest w pliku .json.
    if (!ts && g_object_class_find_property(G_OBJECT_GET_CLASS(mkv), "offset-to-zero"))
        g_object_set(mkv, "offset-to-zero", TRUE, nullptr);
    g_object_set(mux, "muxer", mkv, "async-finalize", FALSE, "start-index", 1,
                 "location", location.toUtf8().constData(), nullptr);
    // Auto-split: po czasie albo po rozmiarze. Ciecie zawsze na klatce kluczowej (GOP = 1 s).
    if (m_cfg.out.splitSeconds > 0)
        g_object_set(mux, "max-size-time", guint64(m_cfg.out.splitSeconds) * GST_SECOND, "send-keyframe-requests", TRUE, nullptr);
    else if (m_cfg.out.splitBytes > 0)
        g_object_set(mux, "max-size-bytes", guint64(m_cfg.out.splitBytes), nullptr);
    gst_bin_add(GST_BIN(bin), mux);
    GstElement* vp = gst_bin_get_by_name(GST_BIN(bin), "vp");
    GstElement* ap = gst_bin_get_by_name(GST_BIN(bin), "ap");
    GstElement* vq = gst_bin_get_by_name(GST_BIN(bin), "vq");
    GstElement* aq = gst_bin_get_by_name(GST_BIN(bin), "aq");
    const bool linked = gst_element_link_pads(vp, "src", mux, "video") && gst_element_link_pads(ap, "src", mux, "audio_%u");
    GstPad* vqSink = gst_element_get_static_pad(vq, "sink");
    GstPad* aqSink = gst_element_get_static_pad(aq, "sink");
    // Znaczniki czasu sa przesuwane tak, zeby plik zaczynal sie od zera - wspolna baza dla obrazu i dzwieku.
    m_recHaveSeg[0] = m_recHaveSeg[1] = false;
    gst_pad_add_probe(vqSink, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Channel::onRecVideoEvent, this, nullptr);
    gst_pad_add_probe(aqSink, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Channel::onRecAudioEvent, this, nullptr);
    gst_pad_add_probe(vqSink, GST_PAD_PROBE_TYPE_BUFFER, &Channel::onRecVideo, this, nullptr);
    gst_pad_add_probe(aqSink, GST_PAD_PROBE_TYPE_BUFFER, &Channel::onRecAudio, this, nullptr);
    GstPad* ghostV = gst_ghost_pad_new("video_sink", vqSink);
    GstPad* ghostA = gst_ghost_pad_new("audio_sink", aqSink);
    gst_element_add_pad(bin, ghostV);
    gst_element_add_pad(bin, ghostA);
    gst_object_unref(vqSink);
    gst_object_unref(aqSink);
    gst_object_unref(vp);
    gst_object_unref(ap);
    gst_object_unref(vq);
    gst_object_unref(aq);
    if (!linked) {
        emit notice(T("%1: cannot build the pipeline").arg(m_cfg.name));
        gst_object_unref(bin);
        return false;
    }

    m_recBase = G_MAXINT64;         // galaz jest dopieta, ale nic nie przechodzi do beginRecordingAt()
    m_stopRt = G_MAXINT64;
    m_firstVideoRt = -1;
    m_lastVideoRt = -1;
    m_recStartClock = 0;
    m_recLosses = 0;
    m_recLostMs = 0;
    {
        std::lock_guard<std::mutex> lock(m_pause.mutex);
        m_pause.phase = PauseState::Run;
        m_pause.reqRt = m_pause.startRt = m_pause.resumeRt = m_pause.total = 0;
        m_pause.audioDiscont = false;
        m_pause.pauses.clear();
    }
    m_pausedMs = 0;

    gst_object_ref(bin);                       // wlasna referencja na czas nagrania
    gst_bin_add(GST_BIN(m_pipeline), bin);
    m_recBin = bin;
    m_recBinShared = bin;
    gst_element_sync_state_with_parent(bin);   // najpierw stan, potem podpiecie do tee

    GstElement* tv = gst_bin_get_by_name(GST_BIN(m_pipeline), "tv");
    GstElement* ta = gst_bin_get_by_name(GST_BIN(m_pipeline), "ta");
    m_teeVideo = gst_element_request_pad_simple(tv, "src_%u");
    m_teeAudio = gst_element_request_pad_simple(ta, "src_%u");
    const bool ok = m_teeVideo && m_teeAudio && gst_pad_link(m_teeVideo, ghostV) == GST_PAD_LINK_OK &&
                    gst_pad_link(m_teeAudio, ghostA) == GST_PAD_LINK_OK;
    gst_object_unref(tv);
    gst_object_unref(ta);
    if (!ok) {
        finishRecording(false, T("cannot build the pipeline"));
        return false;
    }

    m_recFile = QDir(dir).filePath(FileNameTemplate::withPart(pattern, 1) + ext);
    QString sidecarBase = pattern;       // nazwa bez numeru czesci
    sidecarBase.remove(QRegularExpression(QStringLiteral("_?%0\\dd")));
    m_recSidecar = QDir(dir).filePath(sidecarBase + QStringLiteral(".json"));
    m_recSession = session;
    m_recIndex = index;
    m_recStartUtc = QDateTime::currentDateTimeUtc();
    m_parts.clear();
    m_closedBytes = 0;
    m_slowTicks = 0;
    m_slowWarned = false;
    m_encoderName = enc.name;
    m_encoderHw = enc.hardware;
    m_recBytes = m_recBytesPrev = 0;
    m_recMbps = 0;
    m_recClock.start();
    setState(ChannelState::Recording);
    emit notice(T("%1: recording started (%2)").arg(m_cfg.name, EncoderFactory::displayName(enc.name)));
    return true;
}

// Stop: zablokuj wyjscia tee, odepnij galaz, wyslij do niej EOS i poczekaj na EOS z muxera (plik sfinalizowany).
void Channel::beginRecordingAt(quint64 clockNs) {
    if (!m_recBin || m_recStartClock) return;
    m_recStartClock = clockNs ? clockNs : clockNow();
    m_recBase = toRunning(m_recStartClock);
    m_recClock.start();
    m_recStartUtc = QDateTime::currentDateTimeUtc();
}

void Channel::stopRecording(quint64 clockNs) {
    if ((m_state != ChannelState::Recording && m_state != ChannelState::Paused) || !m_recBin) return;
    if (!m_recStartClock) beginRecordingAt(0);                       // Stop przed wyznaczeniem startu
    m_stopRt = clockNs ? toRunning(clockNs) : runningTimeNow();      // wspolna chwila konca dla grupy
    if (m_state == ChannelState::Paused) m_pausedMs += m_pauseClock.elapsed();    // Stop w pauzie: plik konczy sie na poczatku pauzy
    setState(ChannelState::Finalizing);
    // Zrodla sprzetowe (zwlaszcza dzwiek z WASAPI) oddaja dane z opoznieniem: galaz zamykamy dopiero wtedy, gdy obraz
    // i dzwiek doszly do chwili Stop (sondy odrzucaja wszystko od tej chwili), najpozniej po 2 s + latencja zrodla (limit ma znaczenie tylko przy przeciazeniu).
    m_stopSeenVideo = false;
    m_stopSeenAudio = false;
    // zrodla z buforem (RTSP: 2 s) oddaja klatki z opoznieniem rownym latencji pipeline'u - tyle dluzej czekamy
    m_stopWaitMs = 2000;        // limit; zwykle obraz i dzwiek dochodza do chwili Stop w kilkadziesiat ms
    GstQuery* q = gst_query_new_latency();
    if (gst_element_query(m_pipeline, q)) {
        GstClockTime minLat = 0;
        gst_query_parse_latency(q, nullptr, &minLat, nullptr);
        if (GST_CLOCK_TIME_IS_VALID(minLat)) m_stopWaitMs += qMin<qint64>(qint64(minLat / GST_MSECOND), 4000);
    }
    gst_query_unref(q);
    // zrodla sieciowe nie zawsze zglaszaja swoj bufor w zapytaniu o latencje - dla nich czekamy co najmniej 3 s
    if (m_cfg.in.isNetwork()) m_stopWaitMs = qMax<qint64>(m_stopWaitMs, 3000);
    m_stopWait.start();
    m_stopPoll.start();
}

void Channel::closeBranch() {
    m_stopPoll.stop();
    if (!m_recBin || m_state != ChannelState::Finalizing) return;
    gst_pad_add_probe(m_teeVideo, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, &Channel::onStopBlock, this, nullptr);
    gst_pad_add_probe(m_teeAudio, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, &Channel::onStopBlock, this, nullptr);
    m_finalizeTimeout.start();
}

quint64 Channel::clockNow() {
    GstClock* sys = gst_system_clock_obtain();
    const GstClockTime t = gst_clock_get_time(sys);
    gst_object_unref(sys);
    return t;
}

gint64 Channel::toRunning(quint64 clockNs) const {
    return m_pipeline ? gint64(clockNs) - gint64(gst_element_get_base_time(m_pipeline)) : 0;
}

gint64 Channel::runningTimeNow() const { return toRunning(clockNow()); }

qint64 Channel::recordMs() const {
    if (!isRecording()) return 0;
    return m_recClock.elapsed() - m_pausedMs - (m_state == ChannelState::Paused ? m_pauseClock.elapsed() : 0);
}

// Pauza: plik zostaje otwarty, galaz nagrywania dziala dalej, tylko bufory nie przechodza przez sondy.
void Channel::pauseRecording(quint64 clockNs) {
    if (m_state != ChannelState::Recording || !m_recBin) return;
    {
        std::lock_guard<std::mutex> lock(m_pause.mutex);
        if (m_pause.phase == PauseState::ResumePending) {
            m_pause.phase = PauseState::Paused;      // wznowienie jeszcze nie doszlo do skutku - pauza trwa dalej
        } else {
            m_pause.reqRt = clockNs ? toRunning(clockNs) : runningTimeNow();
            m_pause.phase = PauseState::PausePending;
        }
    }
    m_pauseClock.start();
    setState(ChannelState::Paused);
    emit notice(T("%1: recording paused").arg(m_cfg.name));
}

void Channel::resumeRecording(quint64 clockNs) {
    if (m_state != ChannelState::Paused || !m_recBin) return;
    {
        std::lock_guard<std::mutex> lock(m_pause.mutex);
        if (m_pause.phase == PauseState::PausePending) {
            m_pause.phase = PauseState::Run;         // pauza nie zdazyla sie zaczac
            m_pause.audioDiscont = true;
        } else {
            m_pause.reqRt = clockNs ? toRunning(clockNs) : runningTimeNow();
            m_pause.phase = PauseState::ResumePending;
        }
    }
    m_pausedMs += m_pauseClock.elapsed();
    // po wznowieniu od razu klatka kluczowa
    if (GstElement* vp = gst_bin_get_by_name(GST_BIN(m_recBin), "vp")) {
        GstPad* sink = gst_element_get_static_pad(vp, "sink");
        if (GstPad* encSrc = gst_pad_get_peer(sink)) {
            gst_pad_send_event(encSrc, gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, ++m_fkuCount));
            gst_object_unref(encSrc);
        }
        gst_object_unref(sink);
        gst_object_unref(vp);
    }
    setState(ChannelState::Recording);
    emit notice(T("%1: recording resumed").arg(m_cfg.name));
}

// Split: wymus klatke kluczowa w enkoderze i kaz splitmuxsink zamknac plik na najblizszej klatce kluczowej.
void Channel::split() {
    if (m_state != ChannelState::Recording || !m_recBin) return;
    GstElement* vp = gst_bin_get_by_name(GST_BIN(m_recBin), "vp");
    GstElement* mux = gst_bin_get_by_name(GST_BIN(m_recBin), "mux");
    if (vp && mux) {
        GstPad* sink = gst_element_get_static_pad(vp, "sink");
        if (GstPad* encSrc = gst_pad_get_peer(sink)) {       // zdarzenie "w gore" trafia do enkodera
            gst_pad_send_event(encSrc, gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, ++m_fkuCount));
            gst_object_unref(encSrc);
        }
        gst_object_unref(sink);
        g_signal_emit_by_name(mux, "split-now");
    }
    if (vp) gst_object_unref(vp);
    if (mux) gst_object_unref(mux);
}

// Plik opisu nagrania obok plikow wideo: do synchronizacji w montazu i do diagnostyki.
void Channel::writeSidecar(bool clean, const QString& reason) const {
    if (m_recSidecar.isEmpty()) return;
    QJsonArray parts;
    int n = 0;
    for (const RecordPart& p : m_parts) {
        QJsonObject o{{QStringLiteral("part"), ++n},
                      {QStringLiteral("file"), QFileInfo(p.file).fileName()},
                      {QStringLiteral("startMs"), double(p.startNs) / 1e6},
                      {QStringLiteral("bytes"), double(QFileInfo(p.file).size())}};
        if (p.endNs >= 0) o.insert(QStringLiteral("endMs"), double(p.endNs) / 1e6);
        parts.append(o);
    }
    QJsonArray pauses;
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_pause.mutex));
        for (const auto& [at, len] : m_pause.pauses)
            pauses.append(QJsonObject{{QStringLiteral("atMs"), double(at) / 1e6}, {QStringLiteral("durationMs"), double(len) / 1e6}});
    }
    const gint64 baseTime = m_pipeline ? gint64(gst_element_get_base_time(m_pipeline)) : 0;
    const QJsonObject root{
        {QStringLiteral("schema"), QStringLiteral("isorec.recording")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("createdWith"), QStringLiteral("IsoRecorder " APP_VERSION)},
        {QStringLiteral("session"), m_recSession},
        {QStringLiteral("channel"), m_cfg.name},
        {QStringLiteral("index"), m_recIndex},
        {QStringLiteral("source"), sourceText()},
        {QStringLiteral("video"), QJsonObject{{QStringLiteral("width"), m_cfg.width}, {QStringLiteral("height"), m_cfg.height},
                                              {QStringLiteral("fps"), m_cfg.fps}, {QStringLiteral("codec"), QStringLiteral("h264")},
                                              {QStringLiteral("encoder"), m_encoderName}, {QStringLiteral("bitrateKbps"), m_cfg.out.videoKbps}}},
        {QStringLiteral("audio"), QJsonObject{{QStringLiteral("codec"), QStringLiteral("aac")}, {QStringLiteral("bitrateKbps"), m_cfg.out.audioKbps}}},
        {QStringLiteral("container"), m_cfg.out.container},
        {QStringLiteral("startedUtc"), m_recStartUtc.toString(Qt::ISODateWithMs)},
        {QStringLiteral("stoppedUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("pipelineRunningTimeAtStartNs"), double(m_recBase.load())},
        // czasy na wspolnym zegarze wszystkich kanalow - do sprawdzenia i wykorzystania synchronizacji grupy
        {QStringLiteral("startClockNs"), double(m_recStartClock)},
        {QStringLiteral("firstFrameClockNs"), m_firstVideoRt.load() < 0 ? -1.0 : double(m_firstVideoRt.load() + baseTime)},
        {QStringLiteral("lastFrameClockNs"), m_lastVideoRt.load() < 0 ? -1.0 : double(m_lastVideoRt.load() + baseTime)},
        {QStringLiteral("clean"), clean},
        {QStringLiteral("stopReason"), reason},
        {QStringLiteral("pauses"), pauses},
        {QStringLiteral("signalLosses"), m_recLosses},
        {QStringLiteral("signalLostMs"), double(m_recLostMs + (m_signalLost && m_recLosses > 0 ? m_lostClock.elapsed() : 0))},
        {QStringLiteral("parts"), parts}};
    QFile f(m_recSidecar);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(root).toJson());
}

void Channel::finishRecording(bool clean, const QString& reason) {
    if (!m_recBin) return;
    m_finalizeTimeout.stop();
    m_stopPoll.stop();
    GstElement* bin = m_recBin;
    m_recBin = nullptr;
    m_recBinShared = nullptr;

    GstElement* tv = gst_bin_get_by_name(GST_BIN(m_pipeline), "tv");
    GstElement* ta = gst_bin_get_by_name(GST_BIN(m_pipeline), "ta");
    for (auto [tee, pad] : {std::pair{tv, &m_teeVideo}, std::pair{ta, &m_teeAudio}}) {
        if (*pad) {
            if (GstPad* peer = gst_pad_get_peer(*pad)) {   // jeszcze podpiety (wymuszone zamkniecie)
                gst_pad_unlink(*pad, peer);
                gst_object_unref(peer);
            }
            gst_element_release_request_pad(tee, *pad);
            gst_object_unref(*pad);
            *pad = nullptr;
        }
        gst_object_unref(tee);
    }
    gst_element_set_state(bin, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(m_pipeline), bin);
    gst_object_unref(bin);

    pollRecording();
    if (!m_parts.isEmpty() && m_parts.last().endNs < 0) m_parts.last().endNs = (m_recClock.elapsed() - m_pausedMs) * 1000000LL;
    writeSidecar(clean, reason);
    const QString file = m_recFile;
    if (isRecording()) m_state = ChannelState::Preview;
    emit changed();
    const QString name = m_parts.size() > 1 ? T("%1 parts").arg(m_parts.size()) : QFileInfo(file).fileName();
    emit notice(clean ? T("%1: recording saved — %2").arg(m_cfg.name, name)
                      : T("%1: recording interrupted (%2) — %3").arg(m_cfg.name, reason, name));
    emit recordingFinished(file, clean);
}

// Stan zrodla sieciowego. Do pierwszego sygnalu po starcie kanalu to "laczenie", potem kazda przerwa to zanik sygnalu.
void Channel::pollSource() {
    if (!m_pipeline || !m_cfg.in.isNetwork()) return;
    GstElement* fs = gst_bin_get_by_name(GST_BIN(m_pipeline), "fs");
    if (!fs) return;
    // Miara zaniku: czy do pipeline'u idzie obraz zastepczy. Wlasciwosc "status" elementu fallbacksrc tego nie mowi
    // (zrodlo czekajace na polaczenie tez jest "running"), wiec pytamy wewnetrzne przelaczniki: wejscie glowne ma
    // priorytet 0, zastepcze - wyzszy.
    bool lost = false;
    int switches = 0;
    GstIterator* it = gst_bin_iterate_recurse(GST_BIN(fs));
    GValue item = G_VALUE_INIT;
    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        GstElement* el = GST_ELEMENT(g_value_get_object(&item));
        GstElementFactory* f = gst_element_get_factory(el);
        if (f && g_strcmp0(GST_OBJECT_NAME(f), "fallbackswitch") == 0) {
            ++switches;
            GstPad* active = nullptr;
            g_object_get(el, "active-pad", &active, nullptr);
            guint prio = 1;
            if (active) {
                g_object_get(active, "priority", &prio, nullptr);
                gst_object_unref(active);
            }
            lost = lost || prio > 0;
        }
        g_value_reset(&item);
    }
    g_value_unset(&item);
    gst_iterator_free(it);
    gst_object_unref(fs);
    if (switches == 0) lost = true;     // zrodlo jeszcze sie nie zbudowalo
    if (!lost) m_signalSeen = true;
    if (lost == m_signalLost) return;
    m_signalLost = lost;
    if (lost) {
        m_lostClock.start();
        if (m_signalSeen) {
            if (isRecording()) ++m_recLosses;
            emit notice(T("%1: signal lost — reconnecting").arg(m_cfg.name));
        }
    } else {
        if (isRecording() && m_recLosses > 0) m_recLostMs += m_lostClock.elapsed();
        emit notice(T("%1: signal restored").arg(m_cfg.name));
    }
    emit changed();
}

void Channel::pollRecording() {
    if (m_recFile.isEmpty()) return;
    const qint64 size = m_closedBytes + QFileInfo(m_recFile).size();
    m_recMbps = isRecording() ? std::max<qint64>(0, size - m_recBytesPrev) * 8 / 1e6 : 0.0;
    m_recBytesPrev = size;
    m_recBytes = size;
    // Dysk nie nadaza: kolejka przed enkoderem/muxerem zapelniona ponad polowe (1,5 s z 3 s) przez 5 s.
    if (m_state == ChannelState::Recording && m_recBin) {
        if (GstElement* vq = gst_bin_get_by_name(GST_BIN(m_recBin), "vq")) {
            guint64 level = 0;
            g_object_get(vq, "current-level-time", &level, nullptr);
            gst_object_unref(vq);
            m_slowTicks = level > 1500 * GST_MSECOND ? m_slowTicks + 1 : 0;
            if (m_slowTicks >= 5 && !m_slowWarned) {
                m_slowWarned = true;
                emit notice(T("%1: disk or encoder cannot keep up with recording").arg(m_cfg.name));
            }
        }
    }
    if (isRecording()) emit changed();
}

// Watek streamingu. Zrodlo moze w kazdej chwili zaczac nowy segment (zrodla sieciowe robia to po ponownym polaczeniu),
// wiec galaz zapisu ma wlasna os czasu: segment zrodla jest zapamietywany i zastepowany stalym segmentem od zera,
// a znaczniki buforow sa przeliczane segmentem zrodla na running-time i zapisywane jako czas od poczatku nagrania.
GstPadProbeReturn Channel::recEvent(GstPadProbeInfo* info, bool video) {
    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(ev) != GST_EVENT_SEGMENT) return GST_PAD_PROBE_OK;
    const GstSegment* s = nullptr;
    gst_event_parse_segment(ev, &s);
    const int i = video ? 0 : 1;
    m_recHaveSeg[i] = s && s->format == GST_FORMAT_TIME;
    if (m_recHaveSeg[i]) m_recSeg[i] = *s;
    GstSegment clean;
    gst_segment_init(&clean, GST_FORMAT_TIME);
    GstEvent* replacement = gst_event_new_segment(&clean);
    gst_event_set_seqnum(replacement, gst_event_get_seqnum(ev));
    gst_event_unref(ev);
    GST_PAD_PROBE_INFO_DATA(info) = replacement;
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn Channel::onRecVideoEvent(GstPad*, GstPadProbeInfo* info, gpointer user) {
    return static_cast<Channel*>(user)->recEvent(info, true);
}

GstPadProbeReturn Channel::onRecAudioEvent(GstPad*, GstPadProbeInfo* info, gpointer user) {
    return static_cast<Channel*>(user)->recEvent(info, false);
}

// Znacznik w pliku = running-time - poczatek nagrania - suma pauz; ta sama suma dla obrazu i dzwieku.
GstPadProbeReturn Channel::recBuffer(GstPadProbeInfo* info, bool video) {
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    const gint64 base = m_recBase.load();
    if (!GST_BUFFER_PTS_IS_VALID(buf)) return GST_PAD_PROBE_DROP;
    const int si = video ? 0 : 1;
    const GstClockTime rtc = m_recHaveSeg[si] ? gst_segment_to_running_time(&m_recSeg[si], GST_FORMAT_TIME, GST_BUFFER_PTS(buf))
                                              : GST_BUFFER_PTS(buf);
    if (!GST_CLOCK_TIME_IS_VALID(rtc) || gint64(rtc) < base) return GST_PAD_PROBE_DROP;
    const gint64 rt = gint64(rtc);
    if (rt >= m_stopRt.load()) {                                 // po wspolnej chwili Stop
        (video ? m_stopSeenVideo : m_stopSeenAudio) = true;
        return GST_PAD_PROBE_DROP;
    }
    const gint64 dur = GST_BUFFER_DURATION_IS_VALID(buf) ? gint64(GST_BUFFER_DURATION(buf)) : 0;
    gint64 total = 0;
    bool discont = false;
    {
        std::lock_guard<std::mutex> lock(m_pause.mutex);
        PauseState& p = m_pause;
        if (video) {
            switch (p.phase) {
            case PauseState::Run:
                break;
            case PauseState::PausePending:
                if (rt >= p.reqRt) {            // pierwsza klatka po kliknieciu Pause - juz nie trafia do pliku
                    p.startRt = rt;
                    p.phase = PauseState::Paused;
                    return GST_PAD_PROBE_DROP;
                }
                break;
            case PauseState::Paused:
                return GST_PAD_PROBE_DROP;
            case PauseState::ResumePending:
                if (rt < p.reqRt) return GST_PAD_PROBE_DROP;
                // pierwsza klatka po wznowieniu trafia dokladnie w miejsce pierwszej odrzuconej
                p.pauses.emplace_back(p.startRt - base - p.total, rt - p.startRt);
                p.total += rt - p.startRt;
                p.resumeRt = rt;
                p.audioDiscont = true;
                p.phase = PauseState::Run;
                break;
            }
        } else {
            // Dzwiek nigdy nie wychodzi poza granice wyznaczone przez obraz: w razie watpliwosci bufor jest odrzucany
            // (krotka cisza w miejscu ciecia), bo nakladajace sie znaczniki zepsulyby plik.
            switch (p.phase) {
            case PauseState::Run:
                if (rt < p.resumeRt) return GST_PAD_PROBE_DROP;
                break;
            case PauseState::PausePending:
                if (rt < p.resumeRt || rt + dur > p.reqRt) return GST_PAD_PROBE_DROP;
                break;
            default:
                return GST_PAD_PROBE_DROP;
            }
            discont = p.audioDiscont;
            p.audioDiscont = false;
        }
        total = p.total;
    }
    if (video) {
        gint64 none = -1;
        m_firstVideoRt.compare_exchange_strong(none, rt);
        m_lastVideoRt = rt;
    }
    buf = gst_buffer_make_writable(buf);
    GST_BUFFER_PTS(buf) = GstClockTime(rt - base - total);
    GST_BUFFER_DTS(buf) = GST_CLOCK_TIME_NONE;     // dane surowe - DTS nada enkoder
    if (discont) GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DISCONT);
    GST_PAD_PROBE_INFO_DATA(info) = buf;
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn Channel::onRecVideo(GstPad*, GstPadProbeInfo* info, gpointer user) {
    return static_cast<Channel*>(user)->recBuffer(info, true);
}

GstPadProbeReturn Channel::onRecAudio(GstPad*, GstPadProbeInfo* info, gpointer user) {
    return static_cast<Channel*>(user)->recBuffer(info, false);
}

// Watek streamingu: tu wolno tylko odpiac i wyslac EOS; zmiany stanu robi watek GUI po EOS z muxera.
GstPadProbeReturn Channel::onStopBlock(GstPad* pad, GstPadProbeInfo*, gpointer) {
    if (GstPad* peer = gst_pad_get_peer(pad)) {
        gst_pad_unlink(pad, peer);
        gst_pad_send_event(peer, gst_event_new_eos());
        gst_object_unref(peer);
    }
    return GST_PAD_PROBE_REMOVE;
}

// ---------------------------------------------------------------- podglad i szyna

// Watek streamingu GStreamera: kopiujemy klatke i oddajemy ja do watku GUI.
GstFlowReturn Channel::onSample(GstAppSink* sink, gpointer user) {
    auto* self = static_cast<Channel*>(user);
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_OK;
    GstCaps* caps = gst_sample_get_caps(sample);
    GstBuffer* buf = gst_sample_get_buffer(sample);
    GstVideoInfo info;
    GstMapInfo map;
    if (caps && buf && gst_video_info_from_caps(&info, caps) && gst_buffer_map(buf, &map, GST_MAP_READ)) {
        const QImage view(map.data, info.width, info.height,
                          GST_VIDEO_INFO_PLANE_STRIDE(&info, 0), QImage::Format_RGB32);
        QImage copy = view.copy();
        gst_buffer_unmap(buf, &map);
        QMetaObject::invokeMethod(self, [self, copy] {
            self->m_frame = copy;
            emit self->frameChanged();
        }, Qt::QueuedConnection);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

// Watek streamingu: komunikat jest parsowany tutaj, do GUI idzie tylko wynik.
// GST_BUS_DROP oznacza, ze nigdzie w aplikacji nie wolno uzywac gst_bus_pop na tej szynie.
GstBusSyncReply Channel::onBus(GstBus*, GstMessage* msg, gpointer user) {
    auto* self = static_cast<Channel*>(user);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ELEMENT: {
        const GstStructure* st = gst_message_get_structure(msg);
        if (st && gst_structure_has_name(st, "level")) {
            const GValue* v = gst_structure_get_value(st, "peak");
            auto* arr = v ? static_cast<GValueArray*>(g_value_get_boxed(v)) : nullptr;
            std::array<double, 2> db{-100.0, -100.0};
            if (arr && arr->n_values > 0) {
                db[0] = g_value_get_double(&arr->values[0]);
                db[1] = arr->n_values > 1 ? g_value_get_double(&arr->values[1]) : db[0];
            }
            QMetaObject::invokeMethod(self, [self, db] {
                for (size_t i = 0; i < 2; ++i) {
                    const double frac = std::clamp((db[i] + 60.0) / 60.0, 0.0, 1.0);
                    self->m_level[i] = std::max(frac, self->m_level[i] - 0.04);   // opadanie
                }
                emit self->levelsChanged();
            }, Qt::QueuedConnection);
        } else if (st && gst_structure_has_name(st, "GstBinForwarded")) {
            // EOS z galezi nagrywania = muxer zamknal plik
            GstMessage* fwd = nullptr;
            gst_structure_get(st, "message", GST_TYPE_MESSAGE, &fwd, nullptr);
            if (fwd) {
                GstElement* rec = self->m_recBinShared.load();
                if (GST_MESSAGE_TYPE(fwd) == GST_MESSAGE_EOS && rec && GST_MESSAGE_SRC(fwd) == GST_OBJECT(rec))
                    QMetaObject::invokeMethod(self, [self, rec] {
                        if (self->m_recBin == rec) self->finishRecording(true);
                    }, Qt::QueuedConnection);
                gst_message_unref(fwd);
            }
        } else if (st && gst_structure_has_name(st, "splitmuxsink-fragment-opened")) {
            GstClockTime rt = 0;
            gst_structure_get_clock_time(st, "running-time", &rt);
            if (const gchar* loc = gst_structure_get_string(st, "location")) {
                const QString file = QDir::fromNativeSeparators(QString::fromUtf8(loc));
                QMetaObject::invokeMethod(self, [self, file, rt] {
                    if (!self->m_recBin) return;
                    self->m_recFile = file;
                    self->m_parts.append(RecordPart{file, GST_CLOCK_TIME_IS_VALID(rt) ? qint64(rt) : 0, -1});
                    if (self->m_parts.size() > 1)
                        emit self->notice(T("%1: part %2 started").arg(self->m_cfg.name).arg(self->m_parts.size()));
                    emit self->changed();
                }, Qt::QueuedConnection);
            }
        } else if (st && gst_structure_has_name(st, "splitmuxsink-fragment-closed")) {
            GstClockTime rt = 0;
            gst_structure_get_clock_time(st, "running-time", &rt);
            if (const gchar* loc = gst_structure_get_string(st, "location")) {
                const QString file = QDir::fromNativeSeparators(QString::fromUtf8(loc));
                QMetaObject::invokeMethod(self, [self, file, rt] {
                    for (RecordPart& p : self->m_parts)
                        if (p.file == file && p.endNs < 0) p.endNs = GST_CLOCK_TIME_IS_VALID(rt) ? qint64(rt) : 0;
                    // czesc zamknieta w trakcie nagrania (split) - jej rozmiar juz sie nie zmieni
                    if (self->m_recBin && file != self->m_recFile) self->m_closedBytes += QFileInfo(file).size();
                }, Qt::QueuedConnection);
            }
        }
        break;
    }
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        const QString text = err ? QString::fromUtf8(err->message) : T("pipeline error");
        g_clear_error(&err);
        g_free(dbg);
        // Blad wewnatrz galezi nagrywania konczy tylko nagranie - podglad kanalu zostaje.
        GstElement* rec = self->m_recBinShared.load();
        const bool inRecording = rec && GST_MESSAGE_SRC(msg) &&
                                 gst_object_has_as_ancestor(GST_MESSAGE_SRC(msg), GST_OBJECT(rec));
        // Blad wewnatrz galezi wyjscia na zywo wylacza tylko wyjscie.
        GstElement* live = self->m_liveBinShared.load();
        const bool inLive = live && GST_MESSAGE_SRC(msg) && gst_object_has_as_ancestor(GST_MESSAGE_SRC(msg), GST_OBJECT(live));
        const int liveSerial = self->m_liveSerial.load();
        const int gen = self->m_gen.load();
        QMetaObject::invokeMethod(self, [self, text, inRecording, inLive, liveSerial, gen] {
            if (gen != self->m_gen.load()) return;      // komunikat ze starego pipeline'u
            if (inRecording) {
                self->finishRecording(false, text);
            } else if (inLive) {
                // tylko dla biezacego podpiecia; komunikaty z galezi odpietej przez uzytkownika sa pomijane
                if (liveSerial == self->m_liveSerial.load() && self->liveOn()) self->liveError(text);
            } else {
                self->stopPreview();
                self->setState(ChannelState::Error, text);
            }
        }, Qt::QueuedConnection);
        break;
    }
    default:
        break;
    }
    gst_message_unref(msg);
    return GST_BUS_DROP;
}
