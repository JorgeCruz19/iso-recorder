#include "DiskMonitor.h"

#include <QDir>
#include <QHash>
#include <QStorageInfo>

#include <algorithm>

#include "Channel.h"

namespace {
QHash<QString, DiskTestResult>& tests() {
    static QHash<QString, DiskTestResult> t;
    return t;
}
}

QString DiskMonitor::rootFor(const QString& dir) {
    QDir d(dir);
    while (!d.exists() && d.cdUp()) {}       // folder nagran moze jeszcze nie istniec
    const QStorageInfo info(d.absolutePath());
    return info.isValid() ? info.rootPath() : QDir(dir).absolutePath().left(3);
}

QList<VolumeInfo> DiskMonitor::volumes(const QList<Channel*>& channels) {
    QList<VolumeInfo> out;
    auto find = [&](const QString& root) -> VolumeInfo& {
        for (VolumeInfo& v : out)
            if (v.root == root) return v;
        VolumeInfo v;
        v.root = root;
        const QStorageInfo info(root);
        v.free = info.bytesAvailable();
        v.total = info.bytesTotal();
        v.fileSystem = QString::fromLatin1(info.fileSystemType());
        out.append(v);
        return out.last();
    };
    if (channels.isEmpty()) find(rootFor(Channel::defaultOutputDir()));
    for (const Channel* c : channels) {
        // kanal z zapisem podwojnym obciaza dwa wolumeny (albo jeden - gdy oba foldery leza na tym samym dysku)
        QStringList roots;
        for (const QString& dir : c->activeDirs()) {
            const QString r = rootFor(dir);
            if (!roots.contains(r)) roots << r;
        }
        const double mbps = (c->config().out.videoKbps + c->config().out.audioKbps) / 1000.0;
        for (const QString& r : roots) {
            VolumeInfo& v = find(r);
            ++v.channels;
            v.configuredMbps += mbps;
            if (c->isRecording()) {
                ++v.recording;
                v.activeMbps += mbps;
            }
        }
    }
    for (VolumeInfo& v : out) {
        const double mbps = v.requiredMbps();
        v.secondsLeft = mbps > 0 ? qint64(double(v.free) * 8.0 / (mbps * 1e6)) : -1;
    }
    std::sort(out.begin(), out.end(), [](const VolumeInfo& a, const VolumeInfo& b) {
        if ((a.secondsLeft < 0) != (b.secondsLeft < 0)) return b.secondsLeft < 0;
        return a.secondsLeft < b.secondsLeft;
    });
    return out;
}

QString DiskMonitor::formatLeft(qint64 s) {
    if (s < 0) return QStringLiteral("--:--:--");
    return QStringLiteral("%1:%2:%3").arg(s / 3600, 2, 10, QLatin1Char('0')).arg(s / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

DiskMonitor::Verdict DiskMonitor::verdict(const DiskTestResult& r, double requiredMbps) {
    if (!r.ok || requiredMbps <= 0) return Verdict::Unknown;
    const double ratio = r.worstMBs * 8.0 / requiredMbps;
    return ratio >= 3.0 ? Verdict::Good : ratio >= 1.5 ? Verdict::Marginal : Verdict::TooSlow;
}

DiskTestResult DiskMonitor::lastTest(const QString& root) { return tests().value(root); }

void DiskMonitor::setLastTest(const QString& root, const DiskTestResult& r) { tests().insert(root, r); }
