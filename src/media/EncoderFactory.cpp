#include "EncoderFactory.h"

#include <QHash>
#include <gst/gst.h>

namespace {
struct Candidate {
    const char* name;
    const char* label;
    bool hardware;
    bool sessionLimited;     // limit rownoczesnych sesji -> probe powtarzamy przy kazdym Rec
    const char* format;
    const char* args;        // %1 = kbit/s, %2 = GOP w klatkach, %3 = bit/s
};
// Kolejnosc = preferencja trybu "auto". GOP = 1 s (precyzja splitu).
const Candidate candidates[] = {
    {"nvh264enc", "NVIDIA NVENC", true, true, "NV12", "nvh264enc bitrate=%1 gop-size=%2 rc-mode=cbr"},
    {"qsvh264enc", "Intel Quick Sync", true, false, "NV12", "qsvh264enc bitrate=%1 gop-size=%2 rate-control=cbr"},
    {"amfh264enc", "AMD AMF", true, false, "NV12", "amfh264enc bitrate=%1 gop-size=%2 rate-control=cbr"},
    {"mfh264enc", "Media Foundation", true, false, "NV12", "mfh264enc bitrate=%1 gop-size=%2 rc-mode=cbr"},
    {"openh264enc", "OpenH264 (CPU)", false, false, "I420", "openh264enc bitrate=%3 gop-size=%2 rate-control=bitrate complexity=low"},
};

bool present(const char* name) {
    GstElementFactory* f = gst_element_factory_find(name);
    if (!f) return false;
    gst_object_unref(f);
    return true;
}

QString launchFor(const Candidate& c, int fps, int kbps) {
    return QStringLiteral("videoconvert ! video/x-raw,format=%1 ! ").arg(QLatin1String(c.format)) +
           QString::fromLatin1(c.args).arg(kbps).arg(fps).arg(qint64(kbps) * 1000);
}

// Kilka klatek przez prawdziwy enkoder; EOS = dziala, blad albo brak odpowiedzi w 3 s = nie dziala.
bool trial(const Candidate& c, int w, int h, int fps, int kbps) {
    const QString desc = QStringLiteral("videotestsrc num-buffers=10 ! video/x-raw,width=%1,height=%2,framerate=%3/1 ! %4 "
                                        "! h264parse ! fakesink")
                             .arg(w).arg(h).arg(fps).arg(launchFor(c, fps, kbps));
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(desc.toUtf8().constData(), &err);
    if (!p || err) {
        g_clear_error(&err);
        if (p) gst_object_unref(p);
        return false;
    }
    bool ok = false;
    if (gst_element_set_state(p, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
        GstBus* bus = gst_element_get_bus(p);   // ten pipeline ma wlasna szyne, bez mostka do Qt
        if (GstMessage* m = gst_bus_timed_pop_filtered(bus, 3 * GST_SECOND,
                                                       GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR))) {
            ok = GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
            gst_message_unref(m);
        }
        gst_object_unref(bus);
    }
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(p);
    return ok;
}

bool works(const Candidate& c, int w, int h, int fps, int kbps) {
    static QHash<QString, bool> cache;
    if (!present(c.name)) return false;
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(QLatin1String(c.name)).arg(w).arg(h).arg(fps);
    if (!c.sessionLimited) {
        const auto it = cache.constFind(key);
        if (it != cache.constEnd()) return *it;
    }
    const bool ok = trial(c, w, h, fps, kbps);
    cache.insert(key, ok);
    return ok;
}
}

QStringList EncoderFactory::installed() {
    QStringList out;
    for (const Candidate& c : candidates)
        if (present(c.name)) out << QLatin1String(c.name);
    return out;
}

QString EncoderFactory::displayName(const QString& name) {
    for (const Candidate& c : candidates)
        if (name == QLatin1String(c.name)) return QLatin1String(c.label);
    return name;
}

EncoderFactory::Choice EncoderFactory::pick(const QString& preferred, int w, int h, int fps, int kbps) {
    auto make = [&](const Candidate& c) { return Choice{QLatin1String(c.name), launchFor(c, fps, kbps), c.hardware}; };
    for (const Candidate& c : candidates)
        if (preferred == QLatin1String(c.name) && works(c, w, h, fps, kbps)) return make(c);
    for (const Candidate& c : candidates)
        if (works(c, w, h, fps, kbps)) return make(c);
    return {};
}
