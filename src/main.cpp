#include <QApplication>
#include <QTimer>

#include <cstdio>
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
int main(int argc, char* argv[]) {
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
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
