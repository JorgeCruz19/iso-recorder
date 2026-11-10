#pragma once
#include <QList>
#include <QString>

// Urzadzenia wejsciowe widziane przez GStreamera: ekrany (d3d11screencapturesrc), kamery (mfvideosrc)
// i urzadzenia dzwiekowe WASAPI (wejscia oraz wyjscia w trybie loopback = "dzwiek systemowy").
namespace Devices {
struct Info {
    QString name;
    QString id;         // kamera: device-path; dzwiek: device id; ekran: numer (monitor-index) jako tekst
    QString detail;     // ekran: rozdzielczosc
};
struct All {
    QList<Info> screens, cameras, audioInputs, audioLoopbacks;
};
// Wynik jest pamietany; refresh = true wymusza ponowne przeszukanie (trwa do kilkuset ms).
const All& list(bool refresh = false);
}
