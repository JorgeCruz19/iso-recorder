#include "FileNameTemplate.h"

#include <QRegularExpression>

QString FileNameTemplate::sanitize(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text.trimmed()) {
        // '%' jest zarezerwowany dla wzorca numeru czesci
        const bool bad = c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*%").contains(c);
        out += bad ? QLatin1Char('_') : (c.isSpace() ? QLatin1Char('_') : c);
    }
    while (out.endsWith(QLatin1Char('.'))) out.chop(1);
    return out.left(120);
}

QString FileNameTemplate::expand(const QString& tmpl, const Vars& v) {
    static const QRegularExpression re(QStringLiteral("\\{(\\w+)(?::(\\d+))?\\}"));
    const QString src = tmpl.trimmed().isEmpty() ? QString::fromLatin1(kDefault) : tmpl.trimmed();
    QString out;
    bool hasPart = false;
    qsizetype pos = 0;
    auto it = re.globalMatch(src);
    while (it.hasNext()) {
        const auto m = it.next();
        out += sanitize(src.mid(pos, m.capturedStart() - pos));
        pos = m.capturedEnd();
        const QString key = m.captured(1).toLower();
        const int width = m.captured(2).isEmpty() ? 0 : qBound(1, m.captured(2).toInt(), 9);
        if (key == QLatin1String("session")) out += sanitize(v.session);
        else if (key == QLatin1String("channel")) out += sanitize(v.channel);
        else if (key == QLatin1String("source")) out += sanitize(v.source);
        else if (key == QLatin1String("index")) out += QStringLiteral("%1").arg(v.index, width ? width : 2, 10, QLatin1Char('0'));
        else if (key == QLatin1String("date")) out += v.when.toString(QStringLiteral("yyyy-MM-dd"));
        else if (key == QLatin1String("time")) out += v.when.toString(QStringLiteral("HH-mm-ss"));
        else if (key == QLatin1String("part")) { out += QStringLiteral("%0") + QString::number(width ? width : 3) + QLatin1Char('d'); hasPart = true; }
    }
    out += sanitize(src.mid(pos));
    if (!hasPart) out += QStringLiteral("_%03d");   // numer czesci jest obowiazkowy (split)
    return out;
}

QString FileNameTemplate::withPart(const QString& pattern, int part) {
    static const QRegularExpression re(QStringLiteral("%0(\\d)d"));
    const auto m = re.match(pattern);
    if (!m.hasMatch()) return pattern;
    QString out = pattern;
    return out.replace(m.capturedStart(), m.capturedLength(),
                       QStringLiteral("%1").arg(part, m.captured(1).toInt(), 10, QLatin1Char('0')));
}
