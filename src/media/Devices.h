#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Urzadzenia wejsciowe widziane przez GStreamera: ekrany (d3d11screencapturesrc), kamery (mfvideosrc)
// i urzadzenia dzwiekowe WASAPI (wejscia oraz wyjscia w trybie loopback = "dzwiek systemowy").
namespace Devices {
struct Info {
    QString name;
    QString id;         // kamera: device-path; dzwiek: device id; ekran: numer (monitor-index) jako tekst
    QString detail;     // ekran: rozdzielczosc
    QStringList modes;  // kamera: dostepne rozmiary obrazu "WxH", od najwiekszego
};
struct All {
    QList<Info> screens, cameras, audioInputs, audioLoopbacks, audioOutputs;
};
// Wynik jest pamietany; refresh = true wymusza ponowne przeszukanie (trwa do kilkuset ms).
const All& list(bool refresh = false);
// Okna programow nadajace sie do przechwytywania (widoczne, z tytulem). id = uchwyt okna jako liczba.
QList<Info> windows();
// Uchwyt okna o danym tytule (dokladnie, a gdy takiego nie ma - zawierajacego ten tekst); 0 = nie znaleziono.
quint64 findWindow(const QString& title);
// Zrodla NDI widoczne w sieci (name = id = "KOMPUTER (nazwa)"). Pierwsze wywolanie uruchamia wyszukiwanie w tle;
// zrodla pojawiaja sie po kilku sekundach, wiec liste trzeba odpytywac ponownie.
QList<Info> ndiSources();
}
