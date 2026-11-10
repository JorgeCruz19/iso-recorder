#pragma once
#include <QString>
#include <QStringList>

// Wybor enkodera H.264. O przydatnosci decyduje proba rzeczywistego mini-pipeline'u dla danej
// rozdzielczosci i klatkazu - sama obecnosc wtyczki nie wystarcza (instrukcja_koncowa.md, 3.3).
namespace EncoderFactory {
struct Choice {
    QString name;        // np. "qsvh264enc"; puste = nic nie dziala
    QString launch;      // fragment pipeline'u: konwersja formatu + enkoder z ustawieniami
    bool hardware = false;
};
QStringList installed();                 // enkodery obecne w tej instalacji GStreamera, w kolejnosci preferencji
QString displayName(const QString& name);
// preferred: "auto" albo nazwa enkodera. Gdy wybrany nie dziala, bierze nastepny dzialajacy z listy.
Choice pick(const QString& preferred, int width, int height, int fps, int bitrateKbps);
}
