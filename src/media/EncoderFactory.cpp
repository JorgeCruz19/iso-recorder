#include "EncoderFactory.h"

#include <QHash>
#include <gst/gst.h>

namespace {
struct Candidate {
    const char* codec;
    const char* name;
    const char* label;
    bool hardware;
    bool sessionLimited;     // limit rownoczesnych sesji -> probe powtarzamy przy kazdym Rec
    const char* format;
    const char* args;        // %1 = kbit/s, %2 = GOP w klatkach, %3 = bit/s
};
// Kolejnosc w obrebie kodeka = preferencja trybu "auto". GOP = 1 s (precyzja splitu).
// Enkodery programowe tylko na licencjach LGPL / BSD: bez x264 i x265 (GPL). rav1enc jest kilka razy wolniejszy od czasu
// rzeczywistego, avenc_prores_ks takze - dlatego AV1 na procesorze to SVT-AV1, a ProRes to szybki avenc_prores (profil 422).
const Candidate candidates[] = {
    {"h264", "nvh264enc", "NVIDIA NVENC", true, true, "NV12", "nvh264enc bitrate=%1 gop-size=%2 rc-mode=cbr"},
    {"h264", "qsvh264enc", "Intel Quick Sync", true, false, "NV12", "qsvh264enc bitrate=%1 gop-size=%2 rate-control=cbr"},
    {"h264", "amfh264enc", "AMD AMF", true, false, "NV12", "amfh264enc bitrate=%1 gop-size=%2 rate-control=cbr"},
    {"h264", "mfh264enc", "Media Foundation", true, false, "NV12", "mfh264enc bitrate=%1 gop-size=%2 rc-mode=cbr"},
    {"h264", "openh264enc", "OpenH264 (CPU)", false, false, "I420", "openh264enc bitrate=%3 gop-size=%2 rate-control=bitrate complexity=low"},
    {"h265", "nvh265enc", "NVIDIA NVENC", true, true, "NV12", "nvh265enc bitrate=%1 gop-size=%2 rc-mode=cbr"},
    {"h265", "qsvh265enc", "Intel Quick Sync", true, false, "NV12", "qsvh265enc bitrate=%1 gop-size=%2 rate-control=cbr"},
    {"h265", "amfh265enc", "AMD AMF", true, false, "NV12", "amfh265enc bitrate=%1 gop-size=%2 rate-control=cbr"},
    {"h265", "mfh265enc", "Media Foundation", true, false, "NV12", "mfh265enc bitrate=%1 gop-size=%2 rc-mode=cbr"},
    {"av1", "nvav1enc", "NVIDIA NVENC", true, true, "NV12", "nvav1enc bitrate=%1 gop-size=%2 rc-mode=cbr"},
    {"av1", "qsvav1enc", "Intel Quick Sync", true, false, "NV12", "qsvav1enc bitrate=%1 gop-size=%2 rate-control=cbr"},
    {"av1", "amfav1enc", "AMD AMF", true, false, "NV12", "amfav1enc bitrate=%1 gop-size=%2 rate-control=cbr"},
    {"av1", "svtav1enc", "SVT-AV1 (CPU)", false, false, "I420", "svtav1enc target-bitrate=%1 intra-period-length=%2 preset=12"},
    {"prores", "avenc_prores", "ProRes 422 (CPU)", false, false, "I422_10LE", "avenc_prores threads=auto"},
};

const char* parserFor(const char* codec) {
    if (qstrcmp(codec, "h265") == 0) return "h265parse";
    if (qstrcmp(codec, "av1") == 0) return "av1parse";
    if (qstrcmp(codec, "prores") == 0) return "identity";
    return "h264parse";
}

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
                                        "! %5 ! fakesink")
                             .arg(w).arg(h).arg(fps).arg(launchFor(c, fps, kbps), QLatin1String(parserFor(c.codec)));
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

const char* const kCodecs[] = {"h264", "h265", "av1", "prores"};
}

QStringList EncoderFactory::codecs() {
    QStringList out;
    for (const char* codec : kCodecs)
        if (!installed(QLatin1String(codec)).isEmpty()) out << QLatin1String(codec);
    return out;
}

QString EncoderFactory::codecLabel(const QString& codec) {
    if (codec == QLatin1String("h265")) return QStringLiteral("H.265 (HEVC)");
    if (codec == QLatin1String("av1")) return QStringLiteral("AV1");
    if (codec == QLatin1String("prores")) return QStringLiteral("ProRes 422");
    return QStringLiteral("H.264");
}

bool EncoderFactory::fixedBitrate(const QString& codec) { return codec == QLatin1String("prores"); }

QStringList EncoderFactory::installed(const QString& codec) {
    QStringList out;
    for (const Candidate& c : candidates)
        if (codec == QLatin1String(c.codec) && present(c.name)) out << QLatin1String(c.name);
    return out;
}

QString EncoderFactory::displayName(const QString& name) {
    for (const Candidate& c : candidates)
        if (name == QLatin1String(c.name)) return QLatin1String(c.label);
    return name;
}

EncoderFactory::Choice EncoderFactory::pick(const QString& codec, const QString& preferred, int w, int h, int fps, int kbps) {
    auto make = [&](const Candidate& c) {
        return Choice{QLatin1String(c.name), launchFor(c, fps, kbps), c.hardware, QLatin1String(parserFor(c.codec))};
    };
    for (const Candidate& c : candidates)
        if (codec == QLatin1String(c.codec) && preferred == QLatin1String(c.name) && works(c, w, h, fps, kbps)) return make(c);
    for (const Candidate& c : candidates)
        if (codec == QLatin1String(c.codec) && works(c, w, h, fps, kbps)) return make(c);
    return {};
}

bool EncoderFactory::containerAccepts(const QString& container, const QString& codec) {
    if (container == QLatin1String("ts")) return codec == QLatin1String("h264") || codec == QLatin1String("h265");
    if (container == QLatin1String("mp4")) return codec != QLatin1String("prores");
    return container == QLatin1String("mkv") || container == QLatin1String("mov");
}

QString EncoderFactory::containerFor(const QString& container, const QString& codec) {
    if (containerAccepts(container, codec)) return container;
    return codec == QLatin1String("prores") ? QStringLiteral("mov") : QStringLiteral("mkv");
}

const char* EncoderFactory::muxerFor(const QString& container) {
    if (container == QLatin1String("ts")) return "mpegtsmux";
    if (container == QLatin1String("mp4")) return "mp4mux";
    if (container == QLatin1String("mov")) return "qtmux";
    return "matroskamux";
}
