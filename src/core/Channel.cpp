#include "Channel.h"

#include "Lang.h"

#include <gst/video/video.h>

#include <algorithm>

Channel::Channel(const ChannelConfig& cfg, QObject* parent) : QObject(parent), m_cfg(cfg) {}

Channel::~Channel() { stopPreview(); }

void Channel::setName(const QString& name) {
    if (name == m_cfg.name || name.trimmed().isEmpty()) return;
    m_cfg.name = name.trimmed();
    emit changed();
}

void Channel::setPattern(int pattern) {
    if (pattern == m_cfg.pattern) return;
    m_cfg.pattern = pattern;
    restart();
}

void Channel::setAudioWave(int wave) {
    if (wave == m_cfg.audioWave) return;
    m_cfg.audioWave = wave;
    restart();
}

void Channel::setSelected(bool on) {
    if (on == m_selected) return;
    m_selected = on;
    emit changed();
}

QString Channel::formatText() const {
    return QStringLiteral("%1p%2").arg(m_cfg.height).arg(m_cfg.fps);
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
    // tee "tv"/"ta" sa punktami zaczepienia galezi nagrywania (od wersji 0.2).
    const QString desc = QStringLiteral(
        "videotestsrc is-live=true pattern=%1 ! video/x-raw,width=%2,height=%3,framerate=%4/1 ! tee name=tv "
        "tv. ! queue leaky=downstream max-size-buffers=2 ! videorate drop-only=true max-rate=15 "
        "! videoconvertscale ! video/x-raw,format=BGRx,width=480,height=270 "
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

void Channel::stopPreview() {
    if (!m_pipeline) return;
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
// GST_BUS_DROP oznacza, ze nigdzie w aplikacji nie wolno uzywac gst_bus_pop.
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
        QMetaObject::invokeMethod(self, [self, text] {
            self->stopPreview();
            self->setState(ChannelState::Error, text);
        }, Qt::QueuedConnection);
        break;
    }
    default:
        break;
    }
    gst_message_unref(msg);
    return GST_BUS_DROP;
}
