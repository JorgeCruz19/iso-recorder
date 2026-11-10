#include "DiskTest.h"

#include <QDir>
#include <QElapsedTimer>
#include <QStorageInfo>

#include <thread>
#include <vector>
#include <windows.h>

#include "Lang.h"

namespace {
const qint64 kBlock = 1 << 20;                  // 1 MB - wielokrotnosc rozmiaru sektora
const qint64 kMaxTotal = 8LL << 30;             // 8 GB
const qint64 kReserve = 1LL << 30;              // tyle wolnego miejsca musi zostac
}

DiskTest::DiskTest(const QString& dir, int seconds, int streams, QObject* parent)
    : QThread(parent), m_dir(dir), m_seconds(qBound(2, seconds, 600)), m_streams(qBound(1, streams, 16)) {
    qRegisterMetaType<DiskTestResult>();
}

void DiskTest::run() {
    DiskTestResult res;
    res.seconds = m_seconds;
    res.streams = m_streams;
    res.when = QDateTime::currentDateTime();

    QDir().mkpath(m_dir);
    const qint64 free = QStorageInfo(m_dir).bytesAvailable();
    if (free < kReserve + (256LL << 20)) {
        res.error = T("Not enough free space for the test");
        emit done(res);
        return;
    }
    const qint64 limit = qMin(kMaxTotal, free - kReserve);

    std::atomic<qint64> written{0};
    std::atomic<int> longestMs{0};
    std::atomic<bool> stop{false}, failed{false};
    std::vector<std::thread> workers;
    for (int i = 0; i < m_streams; ++i) {
        const QString path = QDir::toNativeSeparators(QDir(m_dir).filePath(QStringLiteral(".isorec_disktest_%1.tmp").arg(i)));
        workers.emplace_back([path, &written, &longestMs, &stop, &failed] {
            const HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                         FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            void* buf = VirtualAlloc(nullptr, SIZE_T(kBlock), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);   // wyrownany do strony
            if (h == INVALID_HANDLE_VALUE || !buf) {
                failed = true;
            } else {
                auto* bytes = static_cast<unsigned char*>(buf);
                for (qint64 k = 0; k < kBlock; ++k) bytes[k] = static_cast<unsigned char>(k * 131 + 17);   // dane niekompresowalne w prosty sposob
                QElapsedTimer t;
                while (!stop) {
                    t.start();
                    DWORD n = 0;
                    if (!WriteFile(h, buf, DWORD(kBlock), &n, nullptr) || n != DWORD(kBlock)) {
                        failed = true;
                        break;
                    }
                    const int ms = int(t.elapsed());
                    int prev = longestMs.load();
                    while (ms > prev && !longestMs.compare_exchange_weak(prev, ms)) {}
                    written += kBlock;
                }
            }
            if (buf) VirtualFree(buf, 0, MEM_RELEASE);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);     // plik znika przy zamknieciu
        });
    }

    QElapsedTimer clock;
    clock.start();
    qint64 prev = 0;
    double worst = -1;
    int second = 0;
    while (second < m_seconds && !m_cancel && !failed && written < limit) {
        const qint64 target = (second + 1) * 1000LL;
        while (clock.elapsed() < target && !m_cancel && !failed) msleep(20);
        const qint64 now = written;
        const double mbs = double(now - prev) / double(1 << 20);
        prev = now;
        ++second;
        if (worst < 0 || mbs < worst) worst = mbs;
        emit progress(second, m_seconds, mbs);
    }
    const double elapsed = clock.elapsed() / 1000.0;
    stop = true;
    for (std::thread& w : workers) w.join();

    if (failed) {
        res.error = T("Cannot write to this folder");
    } else if (second == 0 || m_cancel) {
        res.error = T("Test cancelled");
    } else {
        res.ok = true;
        res.seconds = second;
        res.avgMBs = double(prev) / double(1 << 20) / qMax(0.001, qMin(elapsed, double(second)));
        res.worstMBs = qMax(0.0, worst);
        res.longestWriteMs = longestMs;
    }
    emit done(res);
}
