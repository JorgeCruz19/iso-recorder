#include "AppSettings.h"

#include <QSettings>

#include "ChannelManager.h"

namespace {
QSettings store() { return QSettings(ChannelManager::settingsOrg(), QStringLiteral("IsoRecorder")); }
}

OutputConfig AppSettings::defaultOutput() {
    QSettings s = store();
    OutputConfig o;
    const OutputConfig d;
    o.dir = s.value(QStringLiteral("defaults/dir")).toString();
    o.nameTemplate = s.value(QStringLiteral("defaults/nameTemplate")).toString();
    o.encoder = s.value(QStringLiteral("defaults/encoder"), d.encoder).toString();
    const QString codec = s.value(QStringLiteral("defaults/codec")).toString();
    o.codec = codec == QLatin1String("h265") || codec == QLatin1String("av1") || codec == QLatin1String("prores") ? codec : QStringLiteral("h264");
    o.videoKbps = qBound(500, s.value(QStringLiteral("defaults/videoKbps"), d.videoKbps).toInt(), 200000);
    o.audioKbps = qBound(64, s.value(QStringLiteral("defaults/audioKbps"), d.audioKbps).toInt(), 512);
    const QString cont = s.value(QStringLiteral("defaults/container")).toString();
    o.container = cont == QLatin1String("ts") || cont == QLatin1String("mp4") || cont == QLatin1String("mov") ? cont : QStringLiteral("mkv");
    o.splitSeconds = qMax(0, s.value(QStringLiteral("defaults/splitSeconds"), 0).toInt());
    o.splitBytes = qMax<qint64>(0, s.value(QStringLiteral("defaults/splitBytes"), 0).toLongLong());
    return o;
}

void AppSettings::setDefaultOutput(const OutputConfig& o) {
    QSettings s = store();
    s.setValue(QStringLiteral("defaults/dir"), o.dir);
    s.setValue(QStringLiteral("defaults/nameTemplate"), o.nameTemplate);
    s.setValue(QStringLiteral("defaults/encoder"), o.encoder);
    s.setValue(QStringLiteral("defaults/codec"), o.codec);
    s.setValue(QStringLiteral("defaults/videoKbps"), o.videoKbps);
    s.setValue(QStringLiteral("defaults/audioKbps"), o.audioKbps);
    s.setValue(QStringLiteral("defaults/container"), o.container);
    s.setValue(QStringLiteral("defaults/splitSeconds"), o.splitSeconds);
    s.setValue(QStringLiteral("defaults/splitBytes"), o.splitBytes);
}

QString AppSettings::monitorDevice() { return store().value(QStringLiteral("monitorDevice")).toString(); }
void AppSettings::setMonitorDevice(const QString& id) { store().setValue(QStringLiteral("monitorDevice"), id); }

QString AppSettings::splitFolderTemplate() {
    const QString t = store().value(QStringLiteral("splitFolderTemplate")).toString().trimmed();
    return t.isEmpty() ? QString::fromLatin1(kSplitFolderDefault) : t;
}
QString AppSettings::renameOnSource() {
    const QString m = store().value(QStringLiteral("renameOnSource")).toString();
    return m == QLatin1String("always") || m == QLatin1String("never") ? m : QStringLiteral("ask");
}
void AppSettings::setRenameOnSource(const QString& mode) { store().setValue(QStringLiteral("renameOnSource"), mode); }

void AppSettings::setSplitFolderTemplate(const QString& tmpl) { store().setValue(QStringLiteral("splitFolderTemplate"), tmpl.trimmed()); }
