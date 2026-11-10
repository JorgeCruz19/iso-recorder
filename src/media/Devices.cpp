#include "Devices.h"

#include <gst/gst.h>

namespace {
QString str(const GstStructure* s, const char* field) {
    const gchar* v = s ? gst_structure_get_string(s, field) : nullptr;
    return v ? QString::fromUtf8(v) : QString();
}

Devices::All scan() {
    Devices::All out;
    GstDeviceMonitor* mon = gst_device_monitor_new();
    gst_device_monitor_add_filter(mon, "Source/Monitor", nullptr);
    gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
    gst_device_monitor_add_filter(mon, "Source/Video", nullptr);
    gst_device_monitor_add_filter(mon, "Audio/Source", nullptr);
    GList* devices = gst_device_monitor_get_devices(mon);
    for (GList* l = devices; l; l = l->next) {
        GstDevice* d = GST_DEVICE(l->data);
        gchar* nameC = gst_device_get_display_name(d);
        gchar* klassC = gst_device_get_device_class(d);
        const QString name = QString::fromUtf8(nameC), klass = QString::fromUtf8(klassC);
        g_free(nameC);
        g_free(klassC);
        GstStructure* p = gst_device_get_properties(d);
        if (klass.contains(QLatin1String("Monitor"))) {
            // tylko dostawca d3d11; kolejnosc na liscie = monitor-index
            if (str(p, "device.api") == QLatin1String("d3d11")) {
                int l0 = 0, t0 = 0, r0 = 0, b0 = 0;
                gst_structure_get_int(p, "display.coordinates.left", &l0);
                gst_structure_get_int(p, "display.coordinates.top", &t0);
                gst_structure_get_int(p, "display.coordinates.right", &r0);
                gst_structure_get_int(p, "display.coordinates.bottom", &b0);
                out.screens.append({name, QString::number(out.screens.size()), QStringLiteral("%1×%2").arg(r0 - l0).arg(b0 - t0)});
            }
        } else if (klass.contains(QLatin1String("Video"))) {
            // kamery wystepuja podwojnie (Kernel Streaming i Media Foundation) - bierzemy te z Media Foundation
            const QString path = str(p, "device.path");
            if (!path.isEmpty()) out.cameras.append({name, path, {}});
        } else if (klass.contains(QLatin1String("Audio")) && p && gst_structure_has_field(p, "wasapi2.device.loopback")) {
            gboolean loopback = FALSE, def = FALSE;
            gst_structure_get_boolean(p, "wasapi2.device.loopback", &loopback);
            gst_structure_get_boolean(p, "device.default", &def);
            if (!def) (loopback ? out.audioLoopbacks : out.audioInputs).append({name, str(p, "device.id"), {}});   // domyslne maja wlasna pozycje
        }
        if (p) gst_structure_free(p);
    }
    g_list_free_full(devices, gst_object_unref);
    gst_object_unref(mon);
    return out;
}
}  // namespace

const Devices::All& Devices::list(bool refresh) {
    static Devices::All cache;
    static bool have = false;
    if (!have || refresh) {
        cache = scan();
        have = true;
    }
    return cache;
}
