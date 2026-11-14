#pragma once
#include <QString>
#include <QStringList>

// Wybor enkodera obrazu. O przydatnosci decyduje proba rzeczywistego mini-pipeline'u dla danej
// rozdzielczosci i klatkazu - sama obecnosc wtyczki nie wystarcza (instrukcja_koncowa.md, 3.3).
// Kodeki: "h264", "h265", "av1", "prores" (ProRes 422, staly profil - bitrate wynika z rozmiaru obrazu).
namespace EncoderFactory {
struct Choice {
    QString name;        // np. "qsvh264enc"; puste = nic nie dziala
    QString launch;      // fragment pipeline'u: konwersja formatu + enkoder z ustawieniami
    bool hardware = false;
    QString parser = QStringLiteral("h264parse");    // element za enkoderem (dla ProRes: identity)
};
QStringList codecs();                                // kodeki, dla ktorych jest w tej instalacji choc jeden enkoder
QString codecLabel(const QString& codec);            // "H.264", "H.265 (HEVC)", "AV1", "ProRes 422"
bool fixedBitrate(const QString& codec);             // kodek sam ustala bitrate (ProRes)
QStringList installed(const QString& codec = QStringLiteral("h264"));   // enkodery kodeka, w kolejnosci preferencji
QString displayName(const QString& name);
// preferred: "auto" albo nazwa enkodera. Gdy wybrany nie dziala, bierze nastepny dzialajacy z listy.
Choice pick(const QString& codec, const QString& preferred, int width, int height, int fps, int bitrateKbps);
inline Choice pick(const QString& preferred, int width, int height, int fps, int bitrateKbps) {
    return pick(QStringLiteral("h264"), preferred, width, height, fps, bitrateKbps);
}
// Kontenery: "mkv", "ts", "mp4", "mov". Sprawdzone doswiadczalnie: MPEG-TS przyjmuje H.264 i H.265, MP4 - H.264, H.265
// i AV1, MOV - wszystko, MKV - wszystko.
bool containerAccepts(const QString& container, const QString& codec);
QString containerFor(const QString& container, const QString& codec);    // ten sam albo pierwszy pasujacy
const char* muxerFor(const QString& container);
}
