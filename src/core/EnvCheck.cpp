#include "EnvCheck.h"

#include <QTextStream>
#include <gst/gst.h>

namespace {
struct Item { const char* group; const char* element; bool required; };
const Item items[] = {
    {"Rdzeń", "videotestsrc", true}, {"Rdzeń", "audiotestsrc", true}, {"Rdzeń", "tee", true},
    {"Rdzeń", "queue", true}, {"Rdzeń", "videorate", true}, {"Rdzeń", "videoconvertscale", true},
    {"Rdzeń", "appsink", true}, {"Rdzeń", "level", true}, {"Rdzeń", "splitmuxsink", true},
    {"Kontenery", "matroskamux", true}, {"Kontenery", "mpegtsmux", true}, {"Kontenery", "qtmux", false},
    {"Kontenery", "isofmp4mux", false},
    {"GPU", "d3d11convert", false}, {"GPU", "d3d11upload", false}, {"GPU", "d3d11download", false},
    {"Enkodery wideo", "nvh264enc", false}, {"Enkodery wideo", "qsvh264enc", false},
    {"Enkodery wideo", "amfh264enc", false}, {"Enkodery wideo", "mfh264enc", false},
    {"Enkodery wideo", "openh264enc", false}, {"Enkodery wideo", "qsvh265enc", false},
    {"Enkodery wideo", "avenc_prores_ks", false},
    {"Enkodery audio", "avenc_aac", false}, {"Enkodery audio", "mfaacenc", false}, {"Enkodery audio", "opusenc", false},
    {"Wejście: ekran", "d3d11screencapturesrc", false},
    {"Wejście: HDMI/UVC", "mfvideosrc", false}, {"Wejście: HDMI/UVC", "decklinkvideosrc", false},
    {"Wejście: audio", "wasapi2src", false},
    {"Wejście: NDI", "ndisrc", false}, {"Wejście: NDI", "ndisrcdemux", false},
    {"Wejście: SRT", "srtsrc", false}, {"Wejście: RTSP", "rtspsrc", false},
    {"Wejście: WebRTC", "whepclientsrc", false}, {"Wejście: WebRTC", "whipserversrc", false},
    {"Odporność", "fallbacksrc", false}, {"Timecode", "timecodestamper", false},
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
        out << "  " << (ok ? "[ok]   " : (it.required ? "[BRAK!] " : "[brak] ")) << it.element << "\n";
    }
    out << "\nBrakujące elementy: " << missing
        << (r.requiredOk ? "  (obowiązkowe: komplet)" : "  (BRAKUJE OBOWIĄZKOWYCH)") << "\n";
    return r;
}
