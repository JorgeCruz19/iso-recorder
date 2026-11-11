#include "Devices.h"

#include <gst/gst.h>
#include <windows.h>
#include <dwmapi.h>

#include <algorithm>

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
    gst_device_monitor_add_filter(mon, "Audio/Sink", nullptr);
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
            if (!path.isEmpty()) {
                // rozmiary obrazu, ktore kamera podaje (surowy obraz albo MJPEG)
                QList<QPair<int, int>> sizes;
                if (GstCaps* caps = gst_device_get_caps(d)) {
                    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
                        const GstStructure* st = gst_caps_get_structure(caps, i);
                        int w = 0, h = 0;
                        if (gst_structure_get_int(st, "width", &w) && gst_structure_get_int(st, "height", &h) && w > 0 && h > 0 &&
                            !sizes.contains({w, h}))
                            sizes.append({w, h});
                    }
                    gst_caps_unref(caps);
                }
                std::sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) { return a.first * a.second > b.first * b.second; });
                QStringList modes;
                for (const auto& sz : sizes) modes << QStringLiteral("%1x%2").arg(sz.first).arg(sz.second);
                out.cameras.append({name, path, {}, modes});
            }
        } else if (klass == QLatin1String("Audio/Sink")) {
            // wyjscia dzwieku (odsluch); urzadzenie domyslne ma w ustawieniach wlasna pozycje
            gboolean def = FALSE;
            gst_structure_get_boolean(p, "device.default", &def);
            const QString id = str(p, "device.id");
            if (!def && !id.isEmpty()) out.audioOutputs.append({name, id, {}, {}});
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

QList<Devices::Info> Devices::windows() {
    QList<Info> out;
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto* list = reinterpret_cast<QList<Info>*>(param);
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
        if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
        BOOL cloaked = FALSE;       // okna ukryte przez system (np. na innym pulpicie wirtualnym)
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId()) return TRUE;      // wlasnych okien nie przechwytujemy
        wchar_t title[512];
        const int n = GetWindowTextW(hwnd, title, 512);
        if (n <= 0) return TRUE;
        list->append({QString::fromWCharArray(title, n), QString::number(quint64(reinterpret_cast<quintptr>(hwnd))), {}, {}});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&out));
    return out;
}

quint64 Devices::findWindow(const QString& title) {
    if (title.trimmed().isEmpty()) return 0;
    const QList<Info> all = windows();
    for (const Info& w : all)
        if (w.name == title) return w.id.toULongLong();
    for (const Info& w : all)
        if (w.name.contains(title, Qt::CaseInsensitive)) return w.id.toULongLong();
    return 0;
}

const Devices::All& Devices::list(bool refresh) {
    static Devices::All cache;
    static bool have = false;
    if (!have || refresh) {
        cache = scan();
        have = true;
    }
    return cache;
}
