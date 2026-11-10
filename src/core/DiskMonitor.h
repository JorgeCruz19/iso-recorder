#pragma once
#include <QDateTime>
#include <QList>
#include <QString>

class Channel;

// Wynik testu predkosci zapisu (DiskTest) - pamietany per wolumen do konca sesji programu.
struct DiskTestResult {
    bool ok = false;
    QString error;
    double avgMBs = 0;          // srednia MB/s
    double worstMBs = 0;        // najgorsza sekunda - to ona decyduje o werdykcie
    int longestWriteMs = 0;     // najdluzszy pojedynczy zapis bloku 1 MB
    int seconds = 0;
    int streams = 1;
    QDateTime when;
};

// Kanaly pogrupowane wedlug wolumenu, na ktory zapisuja.
struct VolumeInfo {
    QString root;               // np. "D:/"
    QString fileSystem;
    qint64 free = 0, total = 0;
    int channels = 0;           // kanaly z folderem na tym wolumenie
    int recording = 0;          // z nich nagrywa
    double configuredMbps = 0;  // suma ustawionych bitrate'ow wszystkich kanalow wolumenu
    double activeMbps = 0;      // suma ustawionych bitrate'ow kanalow nagrywajacych
    qint64 secondsLeft = -1;    // czas do zapelnienia; -1 = nie da sie policzyc
    QString name() const { return root.left(2); }
    double requiredMbps() const { return recording > 0 ? activeMbps : configuredMbps; }
};

namespace DiskMonitor {
QString rootFor(const QString& dir);                        // wolumen folderu (folder nie musi jeszcze istniec)
QList<VolumeInfo> volumes(const QList<Channel*>& channels); // posortowane: najkrotszy czas do zapelnienia pierwszy
QString formatLeft(qint64 seconds);                         // HH:MM:SS albo --:--:--
enum class Verdict { Unknown, Good, Marginal, TooSlow };
Verdict verdict(const DiskTestResult& r, double requiredMbps);   // najgorsza sekunda vs potrzebny strumien
DiskTestResult lastTest(const QString& root);
void setLastTest(const QString& root, const DiskTestResult& r);
}
