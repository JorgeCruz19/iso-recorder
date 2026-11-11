#include "EventLog.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>

namespace {
QStringList g_lines;
QString g_dir;
}

void EventLog::enableFile(const QString& dir) {
    g_dir = dir;
    if (g_dir.isEmpty()) return;
    QDir d(g_dir);
    d.mkpath(QStringLiteral("."));
    // porzadki: pliki starsze niz 30 dni
    const QDate limit = QDate::currentDate().addDays(-30);
    for (const QFileInfo& f : d.entryInfoList({QStringLiteral("events_*.log")}, QDir::Files))
        if (f.lastModified().date() < limit) QFile::remove(f.absoluteFilePath());
}

QString EventLog::dir() { return g_dir; }

QString EventLog::todayFile() {
    return g_dir.isEmpty() ? QString() : QDir(g_dir).filePath(QStringLiteral("events_%1.log").arg(QDate::currentDate().toString(Qt::ISODate)));
}

void EventLog::add(const QString& text) {
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QStringLiteral("  ") + text;
    g_lines.append(line);
    if (g_lines.size() > 5000) g_lines.removeFirst();
    if (g_dir.isEmpty()) return;
    QFile f(todayFile());      // otwierany na kazdy wpis: zdarzen jest malo, a plik jest caly takze po awarii
    if (f.open(QIODevice::Append | QIODevice::Text)) f.write((line + QLatin1Char('\n')).toUtf8());
}

QStringList EventLog::recent() { return g_lines; }
