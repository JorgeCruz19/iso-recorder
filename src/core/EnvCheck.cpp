#include "EnvCheck.h"

#include <QTextStream>
#include <gst/gst.h>

namespace {
struct Item { const char* group; const char* element; bool required; };
const Item items[] = {
    {"Core", "videotestsrc", true}, {"Core", "audiotestsrc", true}, {"Core", "tee", true},
    {"Core", "queue", true}, {"Core", "videorate", true}, {"Core", "videoconvertscale", true},
    {"Core", "appsink", true}, {"Core", "level", true}, {"Core", "splitmuxsink", true},
    {"Containers", "matroskamux", true}, {"Containers", "mpegtsmux", true}, {"Containers", "qtmux", false},
    {"Containers", "isofmp4mux", false},
    {"GPU", "d3d11convert", false}, {"GPU", "d3d11upload", false}, {"GPU", "d3d11download", false},
    {"Video encoders", "nvh264enc", false}, {"Video encoders", "qsvh264enc", false},
    {"Video encoders", "amfh264enc", false}, {"Video encoders", "mfh264enc", false},
    {"Video encoders", "openh264enc", false}, {"Video encoders", "qsvh265enc", false},
    {"Video encoders", "avenc_prores_ks", false},
    {"Audio encoders", "avenc_aac", false}, {"Audio encoders", "mfaacenc", false}, {"Audio encoders", "opusenc", false},
    {"Input: screen", "d3d11screencapturesrc", false},
    {"Input: HDMI/UVC", "mfvideosrc", false}, {"Input: HDMI/UVC", "decklinkvideosrc", false},
    {"Input: audio", "wasapi2src", false},
    {"Input: NDI", "ndisrc", false}, {"Input: NDI", "ndisrcdemux", false},
    {"Input: SRT", "srtsrc", false}, {"Input: RTSP", "rtspsrc", false},
    {"Input: WebRTC", "whepclientsrc", false}, {"Input: WebRTC", "whipserversrc", false},
    {"Resilience", "fallbacksrc", false}, {"Timecode", "timecodestamper", false},
};
}

bool EnvCheck::hasElement(const char* name) {
    GstElementFactory* f = gst_element_factory_find(name);
    if (!f) return false;
    gst_object_unref(f);
    return true;
}

EnvCheck::Result EnvCheck::run() {
    Result r;
    QTextStream out(&r.report);
    gchar* ver = gst_version_string();
    out << "ISO Recorder " << APP_VERSION << "\n" << ver << "\n";
    g_free(ver);
    const char* group = "";
    int missing = 0;
    for (const Item& it : items) {
        if (qstrcmp(group, it.group) != 0) {
            group = it.group;
            out << "\n" << QString::fromUtf8(group) << "\n";
        }
        const bool ok = hasElement(it.element);
        if (!ok) {
            ++missing;
            if (it.required) r.requiredOk = false;
        }
        out << "  " << (ok ? "[ok]       " : (it.required ? "[MISSING!] " : "[missing]  ")) << it.element << "\n";
    }
    out << "\nMissing elements: " << missing
        << (r.requiredOk ? "  (required: complete)" : "  (REQUIRED ELEMENTS MISSING)") << "\n";
    return r;
}
