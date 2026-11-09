#pragma once
#include <QList>
#include <QString>

// Jezyk interfejsu. Kluczem jest tekst angielski (jezyk domyslny); brak tlumaczenia = tekst angielski.
namespace Lang {
struct Info { const char* code; const char* name; };
const QList<Info>& available();          // en, pl, de, es, it, fr
void set(const QString& code);
QString current();
QString tr(const char* english);
}

inline QString T(const char* english) { return Lang::tr(english); }

// Wersje, w ktorych pojawia sie zapowiedziane funkcje (instrukcja_koncowa.md, rozdz. 6).
namespace Roadmap {
inline constexpr const char* recording = "0.3";
inline constexpr const char* lists = "0.6";
inline constexpr const char* localInputs = "0.7";
inline constexpr const char* network = "0.8";
inline constexpr const char* ndiDecklink = "0.9";
inline constexpr const char* scheduler = "1.0";
inline constexpr const char* webrtc = "1.1";
}
