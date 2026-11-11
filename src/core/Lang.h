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
inline constexpr const char* localInputs = "1.5";
inline constexpr const char* network = "1.6";
inline constexpr const char* ndi = "2.4";
inline constexpr const char* scheduler = "2.5";
inline constexpr const char* streamCopy = "2.7";
inline constexpr const char* decklink = "2.8";
inline constexpr const char* webrtc = "2.9";
}
