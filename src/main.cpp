#include <QApplication>
#include <QMouseEvent>
#include <QSettings>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <gst/gst.h>
#include <windows.h>

#include "app/ChannelTile.h"
#include "app/DisksDialog.h"
#include "app/Inspector.h"
#include "app/MainWindow.h"
#include "app/Theme.h"
#include "core/ChannelManager.h"
#include "core/DiskTest.h"
#include "core/EnvCheck.h"
#include "core/Lang.h"

// Argumenty:
//   --check-env           raport o elementach GStreamera (kod wyjscia 1, gdy brakuje obowiazkowych)
//   --demo N              start z N kanalami testowymi
//   --lang xx             jezyk interfejsu na te sesje (en, pl, de, es, it, fr)
//   --screenshot plik.png zrzut okna po 4 s i wyjscie (do porownania z mockupem)
//   --rec-test S          test nagrywania: wszystkie kanaly nagrywaja S sekund, potem Stop i wyjscie po finalizacji
//   --cycles N            ile razy powtorzyc cykl Rec/Stop w --rec-test (domyslnie 1)
//   --out folder          folder nagran dla kanalow --demo
//   --container ts|mkv    kontener dla kanalow --demo
//   --auto-split S        auto-split co S sekund dla kanalow --demo
//   --split-every S       w --rec-test: reczny split wszystkich kanalow co S sekund
//   --drag-test A B [plik] przeciagniecie kafla A na miejsce kafla B (numery od 1) zdarzeniami myszy;
//                         kolejnosc kanalow na stdout, opcjonalny zrzut okna w trakcie przeciagania
//   --disk-test folder [S] test predkosci zapisu (domyslnie 10 s), wynik na stdout
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

    const int diskAt = int(args.indexOf(QStringLiteral("--disk-test")));
    if (diskAt >= 0 && diskAt + 1 < args.size()) {
        const int seconds = diskAt + 2 < args.size() && args.at(diskAt + 2).toInt() > 0 ? args.at(diskAt + 2).toInt() : 10;
        DiskTest test(args.at(diskAt + 1), seconds, 4);
        DiskTestResult result;
        QObject::connect(&test, &DiskTest::done, &app, [&](const DiskTestResult& r) { result = r; app.quit(); });
        test.start();
        app.exec();
        test.wait();
        const QByteArray line = (result.ok ? QStringLiteral("ok avg=%1 MB/s worst=%2 MB/s longest_write=%3 ms seconds=%4 streams=%5\n")
                                                 .arg(result.avgMBs, 0, 'f', 1).arg(result.worstMBs, 0, 'f', 1)
                                                 .arg(result.longestWriteMs).arg(result.seconds).arg(result.streams)
                                           : QStringLiteral("error: %1\n").arg(result.error)).toUtf8();
        fwrite(line.constData(), 1, size_t(line.size()), stdout);
        fflush(stdout);
        return result.ok ? 0 : 1;
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
        auto argAfter = [&args](const char* name) {
            const int at = int(args.indexOf(QLatin1String(name)));
            return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : QString();
        };
        for (Channel* c : manager.channels()) {
            OutputConfig o = c->config().out;
            if (!argAfter("--out").isEmpty()) o.dir = argAfter("--out");
            if (!argAfter("--container").isEmpty()) o.container = argAfter("--container");
            if (argAfter("--auto-split").toInt() > 0) o.splitSeconds = argAfter("--auto-split").toInt();
            c->setOutput(o);
        }
        if (const int every = argAfter("--split-every").toInt(); every > 0) {
            auto* t = new QTimer(&manager);
            QObject::connect(t, &QTimer::timeout, &manager, [&manager] {
                for (Channel* c : manager.channels()) c->split();
            });
            QTimer::singleShot(3000, t, [t, every] { t->start(every * 1000); });   // liczone od startu nagrania
        }
        const int recAt = int(args.indexOf(QStringLiteral("--rec-test")));
        if (recAt >= 0 && recAt + 1 < args.size()) {
            const int seconds = qMax(1, args.at(recAt + 1).toInt());
            const int cycAt = int(args.indexOf(QStringLiteral("--cycles")));
            auto* cyclesLeft = new int(cycAt >= 0 && cycAt + 1 < args.size() ? qMax(1, args.at(cycAt + 1).toInt()) : 1);
            auto* startAll = new std::function<void()>;
            *startAll = [&manager, seconds, cyclesLeft, startAll] {
                for (Channel* c : manager.channels()) manager.transport(c, Transport::Rec);
                QTimer::singleShot(seconds * 1000, &manager, [&manager, cyclesLeft, startAll] {
                    manager.stopAllRecording();
                    auto* wait = new QTimer(&manager);          // czekaj na finalizacje wszystkich plikow
                    QObject::connect(wait, &QTimer::timeout, &manager, [&manager, cyclesLeft, startAll, wait] {
                        if (manager.recordingCount() > 0) return;
                        wait->deleteLater();
                        if (--*cyclesLeft > 0) QTimer::singleShot(300, &manager, *startAll);
                        else QTimer::singleShot(500, &manager, [] { QApplication::quit(); });
                    });
                    wait->start(100);
                });
            };
            QTimer::singleShot(3000, &manager, *startAll);
        }
        // --switch-lang xx: po 2 s przelacza jezyk ta sama droga co menu (test przebudowy okna)
        const int switchAt = int(args.indexOf(QStringLiteral("--switch-lang")));
        if (switchAt >= 0 && switchAt + 1 < args.size()) {
            const QString code = args.at(switchAt + 1);
            QTimer::singleShot(2000, &manager, [&w, code] { emit w->languageRequested(code); });
        }
        // --shot-disks plik.png: zrzut okna Disks po 2,5 s i wyjscie
        if (!argAfter("--shot-disks").isEmpty()) {
            auto* dlg = new DisksDialog(&manager, w.get());
            dlg->show();
            const QString path = argAfter("--shot-disks");
            QTimer::singleShot(2500, dlg, [dlg, path] { dlg->grab().save(path); QApplication::quit(); });
        }
        const int dragAt = int(args.indexOf(QStringLiteral("--drag-test")));
        if (dragAt >= 0 && dragAt + 2 < args.size()) {
            const int a = args.at(dragAt + 1).toInt() - 1, b = args.at(dragAt + 2).toInt() - 1;
            const QString shot = dragAt + 3 < args.size() && !args.at(dragAt + 3).startsWith(QLatin1String("--")) ? args.at(dragAt + 3) : QString();
            QTimer::singleShot(2500, &manager, [&w, &manager, a, b, shot] {
                auto tileOf = [&](int i) -> ChannelTile* {
                    if (i < 0 || i >= manager.count()) return nullptr;
                    for (ChannelTile* t : w->findChildren<ChannelTile*>())
                        if (t->channel() == manager.channels().at(i) && t->isVisible()) return t;
                    return nullptr;
                };
                ChannelTile* src = tileOf(a);
                ChannelTile* dst = tileOf(b);
                if (src && dst) {
                    auto send = [src](QEvent::Type type, const QPoint& global, Qt::MouseButton btn, Qt::MouseButtons btns) {
                        QMouseEvent e(type, QPointF(src->mapFromGlobal(global)), QPointF(global), btn, btns, Qt::NoModifier);
                        QApplication::sendEvent(src, &e);
                    };
                    const QPoint from = src->mapToGlobal(src->rect().center());
                    // cel: prawa cwiartka kafla B przy ruchu do przodu listy, lewa przy ruchu do tylu
                    const QPoint to = dst->mapToGlobal(QPoint(b > a ? dst->width() * 3 / 4 : dst->width() / 4, dst->height() / 2));
                    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
                    send(QEvent::MouseMove, from + QPoint(30, 10), Qt::NoButton, Qt::LeftButton);
                    send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
                    if (!shot.isEmpty()) w->grab().save(shot);
                    send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
                }
                QStringList names;
                for (Channel* c : manager.channels()) names << c->config().name;
                const QByteArray line = (names.join(QStringLiteral(", ")) + QLatin1Char('\n')).toUtf8();
                fwrite(line.constData(), 1, size_t(line.size()), stdout);
                fflush(stdout);
                QTimer::singleShot(300, &manager, [] { QApplication::quit(); });
            });
        }
        const int tabAt = int(args.indexOf(QStringLiteral("--tab")));       // 0 Input, 1 Output, 2 Scheduler
        if (tabAt >= 0 && tabAt + 1 < args.size()) w->inspector()->setTab(args.at(tabAt + 1).toInt());
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
