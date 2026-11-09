#include <QApplication>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <gst/gst.h>
#include <windows.h>

#include "app/MainWindow.h"
#include "app/Theme.h"
#include "core/ChannelManager.h"
#include "core/EnvCheck.h"

// Argumenty:
//   --check-env           raport o elementach GStreamera (kod wyjscia 1, gdy brakuje obowiazkowych)
//   --demo N              start z N kanalami testowymi
//   --screenshot plik.png zrzut okna po 4 s i wyjscie (do porownania z mockupem)
// Paczka samodzielna: gdy obok EXE lezy katalog gstreamer-1.0\, GStreamer bierze wtyczki tylko stamtad
// (instalacja systemowa jest ignorowana), a rejestr wtyczek trafia do %LOCALAPPDATA%\IsoRecorder.
static void useBundledGStreamer() {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH * 4];
    if (!GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)))) return;
    const fs::path dir = fs::path(exe).parent_path();
    std::error_code ec;
    if (!fs::is_directory(dir / L"gstreamer-1.0", ec)) return;
    _wputenv_s(L"GST_PLUGIN_SYSTEM_PATH_1_0", (dir / L"gstreamer-1.0").c_str());
    _wputenv_s(L"GST_PLUGIN_PATH_1_0", L"");
    _wputenv_s(L"GST_PLUGIN_PATH", L"");
    _wputenv_s(L"GST_PLUGIN_SCANNER_1_0", (dir / L"gst-plugin-scanner.exe").c_str());
    _wputenv_s(L"GIO_MODULE_DIR", (dir / L"gio" / L"modules").c_str());
    wchar_t local[MAX_PATH * 4];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, DWORD(std::size(local)))) {
        const fs::path data = fs::path(local) / L"IsoRecorder";
        fs::create_directories(data, ec);
        _wputenv_s(L"GST_REGISTRY_1_0", (data / L"gst-registry.bin").c_str());
    }
}

int main(int argc, char* argv[]) {
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    useBundledGStreamer();
    gst_init(&argc, &argv);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("IsoRecorder"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    const QStringList args = app.arguments();

    if (args.contains(QStringLiteral("--check-env"))) {
        const EnvCheck::Result r = EnvCheck::run();
        // Gdy stdout jest przekierowany do pliku/potoku, zostaje; w przeciwnym razie piszemy do konsoli rodzica.
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        const bool redirected = out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN;
        if (!redirected && AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            SetConsoleOutputCP(CP_UTF8);
        }
        const QByteArray utf8 = r.report.toUtf8();
        fwrite(utf8.constData(), 1, size_t(utf8.size()), stdout);
        fflush(stdout);
        return r.requiredOk ? 0 : 1;
    }

    app.setStyleSheet(Theme::styleSheet());
    int rc = 0;
    {
        ChannelManager manager;
        MainWindow w(&manager);
        w.show();

        const int demoAt = int(args.indexOf(QStringLiteral("--demo")));
        if (demoAt >= 0 && demoAt + 1 < args.size()) {
            const int n = qBound(0, args.at(demoAt + 1).toInt(), ChannelManager::MaxChannels);
            for (int i = 0; i < n; ++i) manager.addChannel();
            if (n > 0) {
                manager.setActive(manager.channels().first());
                for (int i = 0; i < qMin(n, 4); ++i) manager.channels().at(i)->setSelected(true);
            }
        }
        const int shotAt = int(args.indexOf(QStringLiteral("--screenshot")));
        if (shotAt >= 0 && shotAt + 1 < args.size()) {
            const QString path = args.at(shotAt + 1);
            QTimer::singleShot(4000, &w, [&w, path] {
                w.grab().save(path);
                QApplication::quit();
            });
        }
        rc = app.exec();
    }   // kanaly i pipeline'y sa zamykane tutaj, przed gst_deinit
    gst_deinit();
    return rc;
}
