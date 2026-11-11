#pragma once
#include <QString>
#include <QStringList>

// Log zdarzen: kazdy komunikat programu (start i koniec nagrania, zaniki sygnalu, bledy, restarty zrodel, listy)
// trafia z data i godzina do pamieci, a przy wlaczonym zapisie takze do pliku dziennego
// <dane programu>\logs\events_RRRR-MM-DD.log. Pliki starsze niz 30 dni sa usuwane przy starcie.
namespace EventLog {
void enableFile(const QString& dir);     // puste = tylko pamiec (tryby testowe)
void add(const QString& text);
QStringList recent();                    // wpisy z tego uruchomienia, od najstarszego (najwyzej 5000)
QString dir();                           // katalog plikow logu; pusty, gdy zapis jest wylaczony
QString todayFile();
}
