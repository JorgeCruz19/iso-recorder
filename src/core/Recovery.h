#pragma once
#include <QString>
#include <QStringList>

// Odzyskiwanie nagran po awarii programu albo zasilania.
// Dziennik (plik obok autozapisu listy) trzyma sciezki plikow opisu nagran, ktore wlasnie trwaja. Wpis znika, gdy nagranie
// konczy sie normalnie - to, co zostaje po starcie programu, zostalo przerwane awaria.
// Naprawa: pliki MKV sa przepisywane do nowego pliku (dostaja dlugosc i indeks - po awarii ich nie maja), MPEG-TS nie
// wymaga niczego, MP4 i MOV sa nie do odzyskania (brak spisu tresci na koncu pliku).
namespace Recovery {
void setEnabled(bool on);               // wylaczone w trybach testowych bez osobnego profilu
void begin(const QString& sidecar);
void end(const QString& sidecar);
QStringList pending();                  // przerwane nagrania, ktorych plik opisu nadal istnieje
void forget(const QStringList& sidecars);
struct Result {
    QString sidecar;
    int parts = 0;          // czesci wymienione w opisie, ktore istnieja
    int repaired = 0;       // przepisane pliki MKV
    int intact = 0;         // pliki, ktore nie wymagaja naprawy (MPEG-TS)
    int lost = 0;           // pliki nie do odzyskania (MP4 / MOV) albo nieudana naprawa
};
// Blokuje do konca naprawy; w trakcie obsluguje zdarzenia Qt (okno nie zamiera). Oryginal naprawionego pliku zostaje
// obok pod nazwa "<nazwa>.crashed.mkv".
Result repair(const QString& sidecar);
}
