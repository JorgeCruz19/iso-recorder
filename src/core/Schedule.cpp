#include "Schedule.h"

#include <QJsonArray>
#include <QTimeZone>

bool ScheduleConfig::active() const {
    for (const ScheduleRule& r : rules)
        if (r.enabled) return true;
    return false;
}

namespace Schedule {

Window windowOn(const ScheduleRule& rule, const QDate& day, int index) {
    Window w;
    if (!rule.enabled || !day.isValid() || !rule.start.isValid() || !rule.stop.isValid()) return w;
    if (rule.weekly ? !(rule.days & (1 << (day.dayOfWeek() - 1))) : rule.date != day) return w;
    // QDateTime w czasie lokalnym sam obsluguje zmiane czasu: godzina, ktorej nie ma (wiosna), przesuwa sie do przodu,
    // a godzina podwojna (jesien) oznacza jej pierwsze wystapienie.
    w.start = QDateTime(day, rule.start);
    w.end = QDateTime(rule.stop > rule.start ? day : day.addDays(1), rule.stop);
    if (!w.start.isValid() || !w.end.isValid() || w.end <= w.start) return Window{};
    w.rule = index;
    return w;
}

Window windowAt(const ScheduleConfig& cfg, const QDateTime& now) {
    Window best;
    for (int i = 0; i < cfg.rules.size(); ++i) {
        for (int back = 0; back <= 1; ++back) {         // okno moglo zaczac sie wczoraj i trwac przez polnoc
            const Window w = windowOn(cfg.rules.at(i), now.date().addDays(-back), i);
            if (w.valid() && w.start <= now && now < w.end && (!best.valid() || w.start < best.start)) best = w;
        }
    }
    return best;
}

Window nextWindow(const ScheduleConfig& cfg, const QDateTime& now) {
    Window best;
    for (int i = 0; i < cfg.rules.size(); ++i) {
        const ScheduleRule& r = cfg.rules.at(i);
        if (!r.enabled) continue;
        if (!r.weekly) {
            const Window w = windowOn(r, r.date, i);
            if (w.valid() && w.start > now && (!best.valid() || w.start < best.start)) best = w;
            continue;
        }
        for (int ahead = 0; ahead <= 8; ++ahead) {
            const Window w = windowOn(r, now.date().addDays(ahead), i);
            if (!w.valid() || w.start <= now) continue;
            if (!best.valid() || w.start < best.start) best = w;
            break;
        }
    }
    return best;
}

QJsonObject toJson(const ScheduleConfig& cfg) {
    QJsonArray rules;
    for (const ScheduleRule& r : cfg.rules) {
        rules.append(QJsonObject{{QStringLiteral("enabled"), r.enabled},
                                 {QStringLiteral("repeat"), r.weekly ? QStringLiteral("weekly") : QStringLiteral("once")},
                                 {QStringLiteral("date"), r.date.toString(Qt::ISODate)},
                                 {QStringLiteral("days"), r.days},
                                 {QStringLiteral("start"), r.start.toString(QStringLiteral("HH:mm:ss"))},
                                 {QStringLiteral("stop"), r.stop.toString(QStringLiteral("HH:mm:ss"))},
                                 {QStringLiteral("splitSeconds"), r.splitSeconds}});
    }
    return QJsonObject{{QStringLiteral("catchUp"), cfg.catchUp}, {QStringLiteral("rules"), rules}};
}

ScheduleConfig fromJson(const QJsonObject& j) {
    ScheduleConfig cfg;
    cfg.catchUp = j.value(QStringLiteral("catchUp")).toBool(true);
    for (const QJsonValue& v : j.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject o = v.toObject();
        ScheduleRule r;
        r.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        r.weekly = o.value(QStringLiteral("repeat")).toString() != QLatin1String("once");
        r.date = QDate::fromString(o.value(QStringLiteral("date")).toString(), Qt::ISODate);
        r.days = o.value(QStringLiteral("days")).toInt(0x7F) & 0x7F;
        const QTime a = QTime::fromString(o.value(QStringLiteral("start")).toString(), QStringLiteral("HH:mm:ss"));
        const QTime b = QTime::fromString(o.value(QStringLiteral("stop")).toString(), QStringLiteral("HH:mm:ss"));
        if (a.isValid()) r.start = a;
        if (b.isValid()) r.stop = b;
        r.splitSeconds = qBound(0, o.value(QStringLiteral("splitSeconds")).toInt(), 24 * 3600);
        if (!r.weekly && !r.date.isValid()) continue;
        cfg.rules.append(r);
        if (cfg.rules.size() >= 32) break;
    }
    return cfg;
}

QString selfTest(int* failed) {
    QString out;
    int bad = 0;
    auto check = [&](const QString& what, bool ok, const QString& got) {
        out += QStringLiteral("%1  %2  (%3)\n").arg(ok ? QStringLiteral("ok   ") : QStringLiteral("BLAD "), what, got);
        if (!ok) ++bad;
    };
    auto dt = [](int y, int mo, int d, int h, int mi, int s = 0) { return QDateTime(QDate(y, mo, d), QTime(h, mi, s)); };
    const QString f = QStringLiteral("yyyy-MM-dd HH:mm:ss");
    auto text = [&](const Window& w) {
        return w.valid() ? w.start.toString(f) + QStringLiteral(" - ") + w.end.toString(f) : QStringLiteral("brak");
    };

    ScheduleConfig c;
    ScheduleRule r;                 // pon-pt 08:00-16:00
    r.days = 0x1F;
    c.rules = {r};
    Window w = windowAt(c, dt(2026, 11, 12, 9, 0));         // czwartek
    check(QStringLiteral("tygodniowa: czwartek 9:00 w oknie"), w.valid() && w.start == dt(2026, 11, 12, 8, 0) && w.end == dt(2026, 11, 12, 16, 0), text(w));
    w = windowAt(c, dt(2026, 11, 12, 16, 0));
    check(QStringLiteral("koniec okna nie nalezy do okna"), !w.valid(), text(w));
    w = windowAt(c, dt(2026, 11, 14, 9, 0));                // sobota
    check(QStringLiteral("sobota poza oknem"), !w.valid(), text(w));
    w = nextWindow(c, dt(2026, 11, 13, 17, 0));             // piatek po oknie -> poniedzialek
    check(QStringLiteral("nastepne okno po piatku to poniedzialek"), w.valid() && w.start == dt(2026, 11, 16, 8, 0), text(w));
    w = nextWindow(c, dt(2026, 11, 12, 7, 59, 59));
    check(QStringLiteral("nastepne okno tego samego dnia"), w.valid() && w.start == dt(2026, 11, 12, 8, 0), text(w));

    r = ScheduleRule{};             // przez polnoc: 22:00-02:00, tylko piatek
    r.days = 1 << 4;
    r.start = QTime(22, 0);
    r.stop = QTime(2, 0);
    c.rules = {r};
    w = windowAt(c, dt(2026, 11, 14, 1, 0));                // sobota 1:00 - okno z piatku
    check(QStringLiteral("okno przez polnoc trwa w sobote"), w.valid() && w.start == dt(2026, 11, 13, 22, 0) && w.end == dt(2026, 11, 14, 2, 0), text(w));
    w = windowAt(c, dt(2026, 11, 14, 23, 0));
    check(QStringLiteral("sobota 23:00 poza oknem piatkowym"), !w.valid(), text(w));

    r = ScheduleRule{};             // jednorazowa
    r.weekly = false;
    r.date = QDate(2026, 12, 24);
    r.start = QTime(18, 0);
    r.stop = QTime(18, 30);
    c.rules = {r};
    w = nextWindow(c, dt(2026, 11, 12, 12, 0));
    check(QStringLiteral("jednorazowa: nastepne okno"), w.valid() && w.start == dt(2026, 12, 24, 18, 0) && w.end == dt(2026, 12, 24, 18, 30), text(w));
    w = nextWindow(c, dt(2026, 12, 24, 18, 0));
    check(QStringLiteral("jednorazowa: po starcie nie ma nastepnego"), !w.valid(), text(w));
    w = windowAt(c, dt(2026, 12, 25, 18, 10));
    check(QStringLiteral("jednorazowa: nastepnego dnia nie obowiazuje"), !w.valid(), text(w));

    // Zmiana czasu - tylko gdy strefa komputera ja stosuje (w 2027 r. w UE: 28 marca i 31 pazdziernika).
    const QTimeZone tz = QTimeZone::systemTimeZone();
    const bool dst = tz.hasDaylightTime() && tz.offsetFromUtc(dt(2027, 7, 1, 12, 0)) - tz.offsetFromUtc(dt(2027, 1, 1, 12, 0)) == 3600 &&
                     tz.offsetFromUtc(dt(2027, 3, 28, 12, 0)) != tz.offsetFromUtc(dt(2027, 3, 27, 12, 0));
    if (dst) {
        r = ScheduleRule{};         // codziennie 01:30-03:30
        r.start = QTime(1, 30);
        r.stop = QTime(3, 30);
        c.rules = {r};
        w = windowOn(r, QDate(2027, 3, 28));
        check(QStringLiteral("wiosna: okno 01:30-03:30 trwa 1 h"), w.valid() && w.start.secsTo(w.end) == 3600, QString::number(w.start.secsTo(w.end)) + QStringLiteral(" s"));
        w = windowOn(r, QDate(2027, 10, 31));
        check(QStringLiteral("jesien: okno 01:30-03:30 trwa 3 h"), w.valid() && w.start.secsTo(w.end) == 3 * 3600, QString::number(w.start.secsTo(w.end)) + QStringLiteral(" s"));
        r.start = QTime(2, 30);     // godzina, ktorej wiosna nie ma
        r.stop = QTime(4, 0);
        w = windowOn(r, QDate(2027, 3, 28));
        check(QStringLiteral("wiosna: start o nieistniejacej 02:30 jest wazny"), w.valid() && w.start < w.end, text(w));
    } else {
        out += QStringLiteral("      zmiana czasu: strefa tego komputera jej nie stosuje - pominiete\n");
    }

    ScheduleConfig rt;              // zapis i odczyt
    r = ScheduleRule{};
    r.weekly = false;
    r.date = QDate(2026, 12, 1);
    r.start = QTime(7, 15, 30);
    r.stop = QTime(7, 45);
    r.splitSeconds = 600;
    r.enabled = false;
    rt.rules = {r, ScheduleRule{}};
    rt.catchUp = false;
    check(QStringLiteral("zapis i odczyt regul"), fromJson(toJson(rt)) == rt, QStringLiteral("%1 reguly").arg(fromJson(toJson(rt)).rules.size()));

    if (failed) *failed = bad;
    return out;
}

}  // namespace Schedule
