#pragma once
#include <QDateTime>
#include <QString>

// Szablon nazwy pliku nagrania. Tokeny: {session} {channel} {source} {index[:N]} {date} {time} {part[:N]}.
namespace FileNameTemplate {
struct Vars {
    QString session, channel, source;
    int index = 1;
    QDateTime when;
};
inline const char* const kDefault = "{session}_{index:02}_{channel}_{date}_{time}_{part:03}";

QString sanitize(const QString& text);                       // usuwa znaki niedozwolone w nazwie pliku
QString expand(const QString& tmpl, const Vars& vars);       // {part} zostaje jako wzorzec printf, np. %03d
QString withPart(const QString& pattern, int part);          // podstawia numer czesci
}
