#include "Widgets.h"

#include <QPainter>
#include <QPainterPath>

#include "Theme.h"
#include "core/Channel.h"
#include "core/Lang.h"

// ---------- KeyButton ----------

KeyButton::KeyButton(const QString& text, Glyph glyph, bool wide, QWidget* parent)
    : QAbstractButton(parent), m_glyph(glyph), m_wide(wide) {
    setText(text);
    // szerokosc dopasowana do napisu - tlumaczenia bywaja dluzsze niz tekst angielski
    m_width = qMax(wide ? 92 : 72, QFontMetrics(Theme::font(10, QFont::Bold, 0.7)).horizontalAdvance(text) + 24);
    setFixedSize(sizeHint());
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void KeyButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.35);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, 4, 4);
    const bool lit = m_lit && m_red;
    if (lit) {
        p.fillPath(path, Theme::red);
    } else {
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, isDown() ? QColor(0x30, 0x30, 0x30) : QColor(0x3e, 0x3e, 0x3e));
        g.setColorAt(1, isDown() ? QColor(0x2b, 0x2b, 0x2b) : QColor(0x33, 0x33, 0x33));
        p.fillPath(path, g);
    }
    p.setPen(QColor(0x12, 0x12, 0x12));
    p.drawPath(path);
    p.setPen(lit ? QColor(0xff, 0x6b, 0x6f) : QColor(0x4d, 0x4d, 0x4d));
    p.drawLine(QPointF(4, 1.5), QPointF(width() - 4, 1.5));

    const QColor fg = lit ? Qt::white : (m_red ? QColor(0xff, 0x5a, 0x5e) : QColor(0xd9, 0xd9, 0xd9));
    p.setPen(Qt::NoPen);
    p.setBrush(fg);
    const double cx = width() / 2.0;
    const bool hasGlyph = m_glyph != None;
    const double gy = 8;   // gorna krawedz symbolu
    switch (m_glyph) {
    case Dot: p.drawEllipse(QRectF(cx - 5.5, gy, 11, 11)); break;
    case Square: p.drawRect(QRectF(cx - 5.5, gy, 11, 11)); break;
    case PauseBars:
        p.drawRect(QRectF(cx - 5.5, gy, 4, 12));
        p.drawRect(QRectF(cx + 1.5, gy, 4, 12));
        break;
    case Cut: {
        p.drawRect(QRectF(cx - 1, gy - 1, 2, 14));
        QPolygonF l, rr;
        l << QPointF(cx - 10, gy + 6) << QPointF(cx - 4, gy + 1) << QPointF(cx - 4, gy + 11);
        rr << QPointF(cx + 10, gy + 6) << QPointF(cx + 4, gy + 1) << QPointF(cx + 4, gy + 11);
        p.drawPolygon(l);
        p.drawPolygon(rr);
        break;
    }
    case Up: {
        QPolygonF t;
        t << QPointF(cx, gy + 2) << QPointF(cx - 6, gy + 10) << QPointF(cx + 6, gy + 10);
        p.drawPolygon(t);
        break;
    }
    case Down: {
        QPolygonF t;
        t << QPointF(cx, gy + 10) << QPointF(cx - 6, gy + 2) << QPointF(cx + 6, gy + 2);
        p.drawPolygon(t);
        break;
    }
    case None: break;
    }
    p.setPen(fg);
    p.setFont(Theme::font(10, QFont::Bold, 0.7));
    const QRect tr = hasGlyph ? QRect(0, 23, width(), 15) : rect();
    p.drawText(tr, Qt::AlignCenter, text());
}

// ---------- PreviewPane ----------

PreviewPane::PreviewPane(QWidget* parent) : QWidget(parent) { setFixedHeight(225); }

// Kadr podgladu jest zawsze 16:9 - wysokosc wynika z szerokosci inspektora.
void PreviewPane::resizeEvent(QResizeEvent*) {
    const int h = width() * 9 / 16;
    if (h > 0 && h != height()) setFixedHeight(h);
}

void PreviewPane::setChannel(Channel* ch, int number) {
    if (m_ch) m_ch->disconnect(this);
    m_ch = ch;
    m_number = number;
    if (ch) {
        connect(ch, &Channel::frameChanged, this, qOverload<>(&QWidget::update));
        connect(ch, &Channel::changed, this, qOverload<>(&QWidget::update));
    }
    update();
}

void PreviewPane::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_ch) {
        p.setPen(Theme::t3);
        p.setFont(Theme::font(12));
        p.drawText(rect(), Qt::AlignCenter, T("No active channel"));
        return;
    }
    const QImage& img = m_ch->frame();
    if (!img.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(rect(), img);   // klatka ma juz 16:9, tak jak ten widget
    }
    // etykieta jak na kaflu
    const QString num = QStringLiteral("%1").arg(m_number, 2, 10, QLatin1Char('0'));
    const QFont fb = Theme::font(12, QFont::Bold), fn = Theme::font(12, QFont::DemiBold);
    const int wn = QFontMetrics(fb).horizontalAdvance(num), wt = QFontMetrics(fn).horizontalAdvance(m_ch->config().name);
    const QRect lr((width() - (wn + 7 + wt + 20)) / 2, height() - 8 - 21, wn + 7 + wt + 20, 21);
    p.fillRect(lr, QColor(0, 0, 0, 209));
    p.setFont(fb);
    p.setPen(QColor(0xbd, 0xbd, 0xbd));
    p.drawText(lr.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, num);
    p.setFont(fn);
    p.setPen(Qt::white);
    p.drawText(lr.adjusted(10 + wn + 7, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, m_ch->config().name);
}

// ---------- MeterPane ----------

MeterPane::MeterPane(QWidget* parent) : QWidget(parent) { setFixedHeight(54); }

void MeterPane::setChannel(Channel* ch) {
    if (m_ch) m_ch->disconnect(this);
    m_ch = ch;
    if (ch) connect(ch, &Channel::levelsChanged, this, qOverload<>(&QWidget::update));
    update();
}

void MeterPane::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Theme::panel);
    const int left = 12 + 20, right = width() - 12;
    p.setFont(Theme::font(10, QFont::Bold));
    const char* names[2] = {"L", "R"};
    for (int i = 0; i < 2; ++i) {
        const int y = 8 + i * 12;
        p.setPen(Theme::t3);
        p.drawText(QRect(12, y - 3, 14, 14), Qt::AlignVCenter | Qt::AlignLeft, QLatin1String(names[i]));
        Theme::paintMeter(p, QRectF(left, y, right - left, 8), m_ch ? m_ch->level(i) : 0.0, false,
                          QColor(0x11, 0x11, 0x11));
    }
    // skala: -60..0 dBFS liniowo
    p.setFont(Theme::font(9));
    p.setPen(Theme::t3);
    for (int db : {-60, -48, -36, -24, -18, -12, -6, 0}) {
        const int x = left + int((right - left) * (db + 60) / 60.0);
        const QString t = QString::number(db);
        const int w = p.fontMetrics().horizontalAdvance(t);
        p.drawText(qBound(left, x - w / 2, right - w), 46, t);
    }
}
