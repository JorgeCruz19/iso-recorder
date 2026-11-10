#include <QApplication>
#include <QSettings>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <gst/gst.h>
#include <windows.h>

#include "app/MainWindow.h"
#include "app/Theme.h"
#include "core/ChannelManager.h"
#include "core/EnvCheck.h"
#include "core/Lang.h"

// Argumenty:
//   --check-env           raport o elementach GStreamera (kod wyjscia 1, gdy brakuje obowiazkowych)
//   --demo N              start z N kanalami testowymi
//   --lang xx             jezyk interfejsu na te sesje (en, pl, de, es, it, fr)
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
    QSettings settings(QStringLiteral("IsoRecorder"), QStringLiteral("IsoRecorder"));
    const int langAt = int(args.indexOf(QStringLiteral("--lang")));
    Lang::set(langAt >= 0 && langAt + 1 < args.size() ? args.at(langAt + 1)
                                                       : settings.value(QStringLiteral("language"), QStringLiteral("en")).toString());
    int rc = 0;
    {
        ChannelManager manager;
        // Zmiana jezyka buduje okno od nowa; kanaly i ich pipeline'y zostaja nietkniete.
        std::unique_ptr<MainWindow> w;
        std::function<void()> makeWindow = [&] {
            MainWindow* old = w.release();
            w = std::make_unique<MainWindow>(&manager);
            QObject::connect(w.get(), &MainWindow::languageRequested, &manager, [&](const QString& code) {
                Lang::set(code);
                settings.setValue(QStringLiteral("language"), Lang::current());
                makeWindow();
            }, Qt::QueuedConnection);
            if (old) {
                w->setGeometry(old->geometry());
                if (old->isMaximized()) w->showMaximized(); else w->show();
                old->hide();
                old->deleteLater();
            } else {
                w->show();
            }
        };
        makeWindow();

        const int demoAt = int(args.indexOf(QStringLiteral("--demo")));
        if (demoAt >= 0 && demoAt + 1 < args.size()) {
            const int n = qBound(0, args.at(demoAt + 1).toInt(), ChannelManager::MaxChannels);
            for (int i = 0; i < n; ++i) manager.addChannel();
            if (n > 0) {
                manager.setActive(manager.channels().first());
                for (int i = 0; i < qMin(n, 4); ++i) manager.channels().at(i)->setSelected(true);
                // kilka kanalow o innych proporcjach - widac wpisanie w kadr 16:9
                const struct { int index, w, h; } odd[] = {{2, 1024, 768}, {3, 720, 1280}, {6, 1920, 800}};
                for (const auto& o : odd)
                    if (o.index < n) manager.channels().at(o.index)->setFormat(o.w, o.h);
            }
        }
        // --switch-lang xx: po 2 s przelacza jezyk ta sama droga co menu (test przebudowy okna)
        const int switchAt = int(args.indexOf(QStringLiteral("--switch-lang")));
        if (switchAt >= 0 && switchAt + 1 < args.size()) {
            const QString code = args.at(switchAt + 1);
            QTimer::singleShot(2000, &manager, [&w, code] { emit w->languageRequested(code); });
        }
        const int shotAt = int(args.indexOf(QStringLiteral("--screenshot")));
        if (shotAt >= 0 && shotAt + 1 < args.size()) {
            const QString path = args.at(shotAt + 1);
            QTimer::singleShot(4000, &manager, [&w, path] {
                w->grab().save(path);
                QApplication::quit();
            });
        }
        rc = app.exec();
    }   // kanaly i pipeline'y sa zamykane tutaj, przed gst_deinit
    gst_deinit();
    return rc;
}
