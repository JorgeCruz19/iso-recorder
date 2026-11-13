#pragma once
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTime>

// Harmonogram nagrywania kanalu (zakladka Scheduler). Regula to okno "od - do": kanal zaczyna nagrywac na poczatku okna,
// konczy na jego koncu, a w trakcie moze dzielic plik co zadany czas. Czasy sa czasem lokalnym komputera.
struct ScheduleRule {
    bool enabled = true;
    bool weekly = true;             // true: w wybrane dni tygodnia; false: jednorazowo, w dniu "date"
    QDate date;                     // regula jednorazowa: dzien poczatku okna
    int days = 0x7F;                // regula tygodniowa: bit 0 = poniedzialek ... bit 6 = niedziela (dzien poczatku okna)
    QTime start = QTime(8, 0);
    QTime stop = QTime(16, 0);      // stop <= start oznacza koniec nastepnego dnia
    int splitSeconds = 0;           // nowa czesc pliku co N sekund liczonych od poczatku okna (0 = bez dzielenia)
    bool operator==(const ScheduleRule&) const = default;
};

struct ScheduleConfig {
    QList<ScheduleRule> rules;
    bool catchUp = true;            // program ruszyl / komputer obudzil sie w trakcie okna: zacznij nagrywac od razu
    bool operator==(const ScheduleConfig&) const = default;
    bool active() const;            // jest przynajmniej jedna wlaczona regula
};

namespace Schedule {
struct Window {
    QDateTime start, end;
    int rule = -1;
    bool valid() const { return rule >= 0; }
};
// Okno obejmujace chwile "now" (najwczesniej zaczete, gdy jest ich kilka) albo niewazne.
Window windowAt(const ScheduleConfig& cfg, const QDateTime& now);
// Najblizsze okno zaczynajace sie po "now" (do 8 dni naprzod dla regul tygodniowych) albo niewazne.
Window nextWindow(const ScheduleConfig& cfg, const QDateTime& now);
// Okno reguly zaczynajace sie w podanym dniu (niewazne, gdy regula w tym dniu nie obowiazuje).
Window windowOn(const ScheduleRule& rule, const QDate& day, int index = 0);
QJsonObject toJson(const ScheduleConfig& cfg);
ScheduleConfig fromJson(const QJsonObject& j);
// Sprawdzenie obliczen (zmiana czasu, okno przez polnoc, reguly tygodniowe); zwraca raport, pusty "failed" = wszystko dobrze.
QString selfTest(int* failed);
}
