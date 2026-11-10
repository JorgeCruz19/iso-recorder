#pragma once
#include <QThread>
#include <atomic>

#include "DiskMonitor.h"

// Test predkosci zapisu: przez zadany czas pisze pliki tymczasowe w folderze docelowym, bez bufora
// systemowego, tyloma rownoleglymi strumieniami, ile kanalow ma pisac na ten wolumen.
// Pliki znikaja same (DELETE_ON_CLOSE). Laczny rozmiar jest ograniczony do 8 GB.
class DiskTest : public QThread {
    Q_OBJECT
public:
    DiskTest(const QString& dir, int seconds, int streams, QObject* parent = nullptr);
    void cancel() { m_cancel = true; }

signals:
    void progress(int second, int ofSeconds, double mbs);
    void done(const DiskTestResult& result);

protected:
    void run() override;

private:
    QString m_dir;
    int m_seconds;
    int m_streams;
    std::atomic<bool> m_cancel{false};
};

Q_DECLARE_METATYPE(DiskTestResult)
