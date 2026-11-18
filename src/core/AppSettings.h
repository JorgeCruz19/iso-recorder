#pragma once
#include <QString>

#include "Channel.h"

// Ustawienia programu (okno Settings), trzymane w rejestrze razem z jezykiem i lista ostatnich plikow.
namespace AppSettings {
// Domyslne parametry nagrywania - dostaje je kazdy nowo dodany kanal.
OutputConfig defaultOutput();
// Domyslny rozmiar obrazu i klatkaz nowo dodawanego kanalu (okno Settings).
struct DefaultFormat { int width = 1280; int height = 720; int fps = 25; };
DefaultFormat defaultFormat();
void setDefaultFormat(const DefaultFormat& f);
void setDefaultOutput(const OutputConfig& out);
// Karta dzwiekowa do odsluchu kanalu (id urzadzenia WASAPI; puste = domyslne wyjscie systemu).
QString monitorDevice();
void setMonitorDevice(const QString& id);
// Szablon nazwy folderu dla "Split to new folder" (te same zmienne co w nazwie pliku, bez {part}).
inline const char* const kSplitFolderDefault = "{session}_{date}_{time}";
QString splitFolderTemplate();
void setSplitFolderTemplate(const QString& tmpl);
// Zmiana nazwy kanalu o domyslnej nazwie po zmianie zrodla: "ask" (pytaj), "always", "never".
QString renameOnSource();
void setRenameOnSource(const QString& mode);
}
