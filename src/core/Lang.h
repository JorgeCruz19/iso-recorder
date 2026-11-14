#pragma once
#include <QList>
#include <QString>

class QTranslator;

// Jezyk interfejsu. Kluczem jest tekst angielski (jezyk domyslny); brak tlumaczenia = tekst angielski.
namespace Lang {
struct Info { const char* code; const char* name; };
const QList<Info>& available();          // en, pl, de, es, it, fr
void set(const QString& code);
QString current();
QString tr(const char* english);
// Tlumacz dla standardowych przyciskow Qt (OK, Cancel, Yes, No, Save, Discard...) - instalowany raz w QApplication;
// bierze teksty z tej samej tabeli, wiec idzie za biezacym jezykiem.
QTranslator* buttonTranslator();
}

inline QString T(const char* english) { return Lang::tr(english); }

// Wersje, w ktorych pojawia sie zapowiedziane funkcje (instrukcja_koncowa.md, rozdz. 6).
namespace Roadmap {
inline constexpr const char* localInputs = "1.5";
inline constexpr const char* network = "1.6";
inline constexpr const char* ndi = "2.5";
inline constexpr const char* scheduler = "2.8";
inline constexpr const char* streamCopy = "2.9";
inline constexpr const char* decklink = "3.1";
inline constexpr const char* webrtc = "3.3";
}
