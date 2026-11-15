#pragma once
#include <QImage>

// Ekran powitalny w osobnym watku systemowym: znika dokladnie po zadanym czasie, takze gdy watek glowny jest zajety
// startem programu (budowa okna, przeszukiwanie urzadzen) i nie obsluguje zdarzen.
namespace Splash {
void show(const QImage& image, int milliseconds);   // obraz w pikselach ekranu, wysrodkowany na ekranie glownym
void close();                                       // wczesniejsze zamkniecie (np. przed pytaniem przy starcie)
}
