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

Channel::Channel(const ChannelConfig& cfg, QObject* parent) : QObject(parent), m_cfg(cfg) {
    m_finalizeTimeout.setSingleShot(true);
    m_finalizeTimeout.setInterval(10000);
    connect(&m_finalizeTimeout, &QTimer::timeout, this, [this] { finishRecording(false, T("finalization timed out")); });
}

Channel::~Channel() { stopPreview(); }

void Channel::setName(const QString& name) {
    if (name == m_cfg.name || name.trimmed().isEmpty()) return;
    m_cfg.name = name.trimmed();
    emit changed();
}

void Channel::setPattern(int pattern) {
    if (pattern == m_cfg.pattern || !canEdit()) return;
    m_cfg.pattern = pattern;
    restart();
}

void Channel::setAudioWave(int wave) {
    if (wave == m_cfg.audioWave || !canEdit()) return;
    m_cfg.audioWave = wave;
    restart();
}

void Channel::setFormat(int width, int height) {
    if ((width == m_cfg.width && height == m_cfg.height) || !canEdit()) return;
    m_cfg.width = width;
    m_cfg.height = height;
    restart();
}

void Channel::setOutput(const OutputConfig& out) {
    if (!canEdit()) return;
    m_cfg.out = out;
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

QString Channel::sourceText() const {
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

void Channel::startPreview() {
    if (m_pipeline) return;
    // tee "tv"/"ta" sa punktami zaczepienia galezi nagrywania.
    const QString desc = QStringLiteral(
        "videotestsrc is-live=true pattern=%1 ! video/x-raw,width=%2,height=%3,framerate=%4/1,pixel-aspect-ratio=1/1 ! tee name=tv "
        "tv. ! queue leaky=downstream max-size-buffers=2 ! videorate name=pvrate drop-only=true max-rate=15 "
        // staly kadr 16:9 i kwadratowy piksel: videoconvertscale sam dopisuje czarne pasy dla innych proporcji
        "! videoconvertscale add-borders=true "
        "! capsfilter name=pvcaps caps=video/x-raw,format=BGRx,width=480,height=270,pixel-aspect-ratio=1/1 "
        "! appsink name=preview sync=false max-buffers=1 drop=true "
        "audiotestsrc is-live=true wave=%5 volume=%6 ! audio/x-raw,rate=48000,channels=2 "
        "! level name=level interval=100000000 post-messages=true ! tee name=ta "
        "ta. ! queue ! fakesink sync=false")
        .arg(m_cfg.pattern).arg(m_cfg.width).arg(m_cfg.height).arg(m_cfg.fps)
        .arg(m_cfg.audioWave).arg(QString::number(m_cfg.audioVolume, 'f', 3));

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
    applyPreviewSize();

    GstElement* sink = gst_bin_get_by_name(GST_BIN(m_pipeline), "preview");
    GstAppSinkCallbacks cb{};
    cb.new_sample = &Channel::onSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &cb, this, nullptr);
    gst_object_unref(sink);

    GstBus* bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, &Channel::onBus, this, nullptr);
    gst_object_unref(bus);

    if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        stopPreview();
        setState(ChannelState::Error, T("cannot start the preview"));
        return;
    }
    setState(ChannelState::Preview);
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
    // NULL zatrzymuje watki streamingu synchronicznie - po tym zaden callback juz nie wejdzie.
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
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

    m_recBase = runningTimeNow();
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
void Channel::stopRecording() {
    if ((m_state != ChannelState::Recording && m_state != ChannelState::Paused) || !m_recBin) return;
    if (m_state == ChannelState::Paused) m_pausedMs += m_pauseClock.elapsed();    // Stop w pauzie: plik konczy sie na poczatku pauzy
    setState(ChannelState::Finalizing);
    gst_pad_add_probe(m_teeVideo, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, &Channel::onStopBlock, this, nullptr);
    gst_pad_add_probe(m_teeAudio, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, &Channel::onStopBlock, this, nullptr);
    m_finalizeTimeout.start();
}

gint64 Channel::runningTimeNow() const {
    if (!m_pipeline) return 0;
    GstClock* clock = gst_element_get_clock(m_pipeline);
    if (!clock) return 0;
    const gint64 rt = gint64(gst_clock_get_time(clock) - gst_element_get_base_time(m_pipeline));
    gst_object_unref(clock);
    return rt;
}

qint64 Channel::recordMs() const {
    if (!isRecording()) return 0;
    return m_recClock.elapsed() - m_pausedMs - (m_state == ChannelState::Paused ? m_pauseClock.elapsed() : 0);
}

// Pauza: plik zostaje otwarty, galaz nagrywania dziala dalej, tylko bufory nie przechodza przez sondy.
void Channel::pauseRecording() {
    if (m_state != ChannelState::Recording || !m_recBin) return;
    {
        std::lock_guard<std::mutex> lock(m_pause.mutex);
        if (m_pause.phase == PauseState::ResumePending) {
            m_pause.phase = PauseState::Paused;      // wznowienie jeszcze nie doszlo do skutku - pauza trwa dalej
        } else {
            m_pause.reqRt = runningTimeNow();
            m_pause.phase = PauseState::PausePending;
        }
    }
    m_pauseClock.start();
    setState(ChannelState::Paused);
    emit notice(T("%1: recording paused").arg(m_cfg.name));
}

void Channel::resumeRecording() {
    if (m_state != ChannelState::Paused || !m_recBin) return;
    {
        std::lock_guard<std::mutex> lock(m_pause.mutex);
        if (m_pause.phase == PauseState::PausePending) {
            m_pause.phase = PauseState::Run;         // pauza nie zdazyla sie zaczac
            m_pause.audioDiscont = true;
        } else {
            m_pause.reqRt = runningTimeNow();
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
        {QStringLiteral("clean"), clean},
        {QStringLiteral("stopReason"), reason},
        {QStringLiteral("pauses"), pauses},
        {QStringLiteral("parts"), parts}};
    QFile f(m_recSidecar);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(root).toJson());
}

void Channel::finishRecording(bool clean, const QString& reason) {
    if (!m_recBin) return;
    m_finalizeTimeout.stop();
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

// Watek streamingu. Zalozenie tej wersji: PTS == running-time (zrodla "live" z segmentem od zera);
// wejscia z innym segmentem beda tu przeliczane przez gst_segment_to_running_time.
// Znacznik w pliku = running-time - poczatek nagrania - suma pauz; ta sama suma dla obrazu i dzwieku.
GstPadProbeReturn Channel::recBuffer(GstPadProbeInfo* info, bool video) {
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    const gint64 base = m_recBase.load();
    if (!GST_BUFFER_PTS_IS_VALID(buf) || gint64(GST_BUFFER_PTS(buf)) < base) return GST_PAD_PROBE_DROP;
    const gint64 rt = gint64(GST_BUFFER_PTS(buf));
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
        QMetaObject::invokeMethod(self, [self, text, inRecording] {
            if (inRecording) {
                self->finishRecording(false, text);
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
