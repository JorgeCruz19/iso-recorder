#pragma once
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QString>

// Jedno zrodlo prawdy dla kolorow i typografii - wartosci z mockup/interfejs.html (:root).
namespace Theme {
inline const QColor bg{0x1f, 0x1f, 0x1f}, bar{0x26, 0x26, 0x26}, panel{0x2a, 0x2a, 0x2a}, sec{0x23, 0x23, 0x23},
    field{0x1a, 0x1a, 0x1a}, b1{0x15, 0x15, 0x15}, t1{0xd6, 0xd6, 0xd6}, t2{0x9d, 0x9d, 0x9d}, t3{0x6e, 0x6e, 0x6e},
    red{0xe0, 0x24, 0x2b}, amber{0xf2, 0xa5, 0x16}, green{0x1f, 0xae, 0x52}, orange{0xe8, 0x84, 0x2a},
    white{0xf0, 0xf0, 0xf0}, idle{0x3c, 0x3c, 0x3c}, meterYellow{0xe7, 0xc1, 0x1c};

inline QFont font(int px, QFont::Weight weight = QFont::Normal, qreal spacing = 0) {
    QFont f;
    f.setFamilies({QStringLiteral("Open Sans"), QStringLiteral("Segoe UI")});
    f.setPixelSize(px);
    f.setWeight(weight);
    if (spacing != 0) f.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    return f;
}

// Miernik audio: zielony do 70%, zolty do 88%, czerwony powyzej (jak w mockupie).
inline void paintMeter(QPainter& p, const QRectF& r, double frac, bool vertical, const QColor& back) {
    p.fillRect(r, back);
    const struct { double from, to; QColor c; } seg[] = {{0, .70, green}, {.70, .88, meterYellow}, {.88, 1, red}};
    for (const auto& s : seg) {
        const double to = qMin(frac, s.to);
        if (to <= s.from) break;
        if (vertical)
            p.fillRect(QRectF(r.left(), r.bottom() - r.height() * to, r.width(), r.height() * (to - s.from)), s.c);
        else
            p.fillRect(QRectF(r.left() + r.width() * s.from, r.top(), r.width() * (to - s.from), r.height()), s.c);
    }
}

// Ikona sluchawek (odsluch kanalu): wlaczona - pomaranczowa, wylaczona - ciemna z jasnym rysunkiem.
inline void paintHeadphones(QPainter& p, const QRectF& r, bool on) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(on ? orange : QColor(0, 0, 0, 178));
    p.drawRoundedRect(r, 3, 3);
    const QColor fg = on ? QColor(0x14, 0x0a, 0x00) : QColor(0xd6, 0xd6, 0xd6);
    const double w = r.width(), h = r.height();
    const QRectF band(r.left() + w * 0.24, r.top() + h * 0.20, w * 0.52, h * 0.80);
    p.setPen(QPen(fg, qMax(1.4, h / 9.0)));
    p.setBrush(Qt::NoBrush);
    p.drawArc(band, 0, 180 * 16);
    p.setPen(Qt::NoPen);
    p.setBrush(fg);
    const double cupW = qMax(3.0, w * 0.18), cupH = h * 0.36, cupY = r.top() + h * 0.50;
    p.drawRoundedRect(QRectF(band.left() - cupW / 2, cupY, cupW, cupH), 1.5, 1.5);
    p.drawRoundedRect(QRectF(band.right() - cupW / 2, cupY, cupW, cupH), 1.5, 1.5);
    p.restore();
}

// arrow / arrowOff: sciezki do obrazkow strzalki listy rozwijanej (QSS nie umie jej narysowac sam - trik z ramkami
// dawal kwadracik), tworzonych przy starcie programu.
inline QString styleSheet(const QString& arrow = {}, const QString& arrowOff = {}) {
    return QStringLiteral(R"(
QWidget { background:#1f1f1f; color:#d6d6d6; font-family:"Open Sans","Segoe UI"; font-size:12px; }
QToolTip { background:#141414; color:#d6d6d6; border:1px solid #3b3b3b; }
#header, #controlPanel { background:#262626; }
#header { border-bottom:1px solid #151515; }
#controlPanel { border-top:1px solid #151515; border-left:1px solid #151515; }
#cell { background:#262626; border-right:1px solid #151515; }
#grp { background:#262626; border-bottom:1px solid #151515; }
#cell QLabel, #grp QLabel, #viewsel QLabel { background:transparent; }
#viewsel { background:#262626; }
QLabel#cap { font-size:10px; font-weight:700; color:#6e6e6e; }
QLabel#proj { font-size:15px; font-weight:600; }
QLabel#tc { font-size:38px; color:#f0f0f0; }
QLabel#tcSmall { font-size:22px; }
QLabel#big { font-size:17px; color:#f0f0f0; }
QLabel#stat { font-size:15px; font-weight:600; }
QLabel#dim { color:#9d9d9d; }
QLabel#recBadge { background:#e0242b; color:#fff; font-weight:700; font-size:11px; padding:3px 8px; border-radius:2px; }
QLabel#countN { font-size:13px; font-weight:700; color:#f0f0f0; }
QProgressBar { background:#111; border:1px solid #0c0c0c; max-height:5px; min-height:5px; }
QProgressBar::chunk { background:#8a8a8a; }
QPushButton#vb { background:#333; border:1px solid #151515; color:#9d9d9d; font-size:11px; font-weight:600;
                 min-height:24px; min-width:30px; padding:0 10px; border-radius:0; }
QPushButton#vb:checked { background:#4a4a4a; color:#f0f0f0; }
QPushButton#vb::menu-indicator { image:none; }
#inspector, #inspector QWidget { background:#2a2a2a; }
#inspector { border-left:1px solid #151515; }
#inspector QLabel#ih { background:#262626; border-bottom:1px solid #151515; font-weight:600; padding:0 12px;
                       min-height:33px; max-height:33px; }
#inspector QPushButton#itab { background:#262626; color:#9d9d9d; font-weight:600; border:0;
                              border-right:1px solid #151515; border-bottom:1px solid #151515; padding:8px 0; }
#inspector QPushButton#itab:checked { background:#2a2a2a; color:#f0f0f0; border-top:2px solid #e8842a;
                                      border-bottom:1px solid #2a2a2a; }
#inspector QLabel#sh { background:#232323; border-top:1px solid #333; border-bottom:1px solid #151515;
                       font-weight:600; padding:0 12px; min-height:26px; max-height:26px; }
#inspector QLabel#shOff { background:#232323; border-top:1px solid #333; border-bottom:1px solid #151515;
                          font-weight:600; color:#9d9d9d; padding:0 12px; min-height:26px; max-height:26px; }
#inspector QLabel#fl { color:#9d9d9d; font-size:11px; }
#inspector QLabel#note { color:#6e6e6e; padding:14px 12px; }
#inspector QLabel#lock { color:#f2a516; padding:6px 12px; font-size:11px; }
#inspector QPushButton#ibtn { background:#3a3a3a; border:1px solid #151515; border-radius:2px; color:#d6d6d6;
                              font-size:11px; font-weight:600; min-height:22px; padding:0 10px; }
#inspector QPushButton#ibtn:pressed { background:#303030; }
#inspector QPushButton#ibtn:disabled { color:#6e6e6e; }
#inspector QComboBox:disabled, #inspector QLineEdit:disabled { color:#6e6e6e; }
#inspector QComboBox, #inspector QLineEdit { background:#1a1a1a; border:1px solid #3a3a3a; border-radius:2px;
                                             padding:0 8px; min-height:21px; font-size:11px; }
#inspector QComboBox::drop-down { border:0; width:18px; }
#inspector QComboBox::down-arrow { image:url(%ARROW%); width:9px; height:6px; margin-right:8px; }
#inspector QComboBox::down-arrow:disabled { image:url(%ARROW_OFF%); }
#inspector QComboBox QAbstractItemView { background:#1a1a1a; border:1px solid #3a3a3a;
                                         selection-background-color:#4a4a4a; }
#status { background:#161616; border-top:1px solid #000; }
#status QLabel { background:transparent; color:#9d9d9d; font-size:11px; }
QMenu { background:#262626; border:1px solid #151515; padding:4px 0; }
QMenu::item { padding:5px 22px; background:transparent; }
QMenu::item:selected { background:#4a4a4a; }
QMenu::item:disabled { color:#6e6e6e; }
QMenu::separator { height:1px; background:#151515; margin:4px 0; }
QScrollBar:vertical { background:#0b0b0b; width:10px; }
QScrollBar::handle:vertical { background:#3b3b3b; min-height:30px; }
QScrollBar::add-line, QScrollBar::sub-line { height:0; }
QPlainTextEdit { background:#1a1a1a; border:1px solid #3a3a3a; font-family:Consolas; }
QTableWidget { background:#1a1a1a; border:1px solid #3a3a3a; }
QTableWidget::item { padding:4px 10px; border-bottom:1px solid #262626; }
QHeaderView::section { background:#262626; color:#9d9d9d; border:0; border-bottom:1px solid #151515; padding:5px 10px;
                       font-size:11px; font-weight:600; }
QPushButton#dbtn { background:#3a3a3a; border:1px solid #151515; border-radius:2px; padding:3px 10px; font-size:11px; font-weight:600; }
QPushButton#dbtn:disabled { color:#6e6e6e; }
QDialog QPushButton { background:#3a3a3a; border:1px solid #151515; border-radius:2px; padding:4px 14px; }
)")
        .replace(QLatin1String("%ARROW_OFF%"), arrowOff).replace(QLatin1String("%ARROW%"), arrow);
}
}
