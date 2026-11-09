#include "ChannelTile.h"

#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>

#include "Theme.h"
#include "core/ChannelManager.h"
#include "core/Lang.h"

namespace {
const char* const kBtn[4] = {"REC", "II", "STOP", "SPLIT"};
const int kStripH = 26;
}

ChannelTile::ChannelTile(ChannelManager* mgr, Channel* ch, int number, QWidget* parent)
    : QWidget(parent), m_mgr(mgr), m_ch(ch), m_number(number) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(ch, &Channel::frameChanged, this, qOverload<>(&QWidget::update));
    connect(ch, &Channel::levelsChanged, this, qOverload<>(&QWidget::update));
    connect(ch, &Channel::changed, this, qOverload<>(&QWidget::update));
    connect(mgr, &ChannelManager::activeChanged, this, qOverload<>(&QWidget::update));
}

ChannelTile::Rects ChannelTile::rects() const {
    Rects r;
    r.vid = QRect(0, 0, width(), height() - kStripH);
    r.strip = QRect(0, height() - kStripH, width(), kStripH);
    r.selBox = QRect(8, 7, 14, 14);
    const QFontMetrics fm(Theme::font(10, QFont::Bold, 0.5));
    int x = 4;
    for (int i = 0; i < 4; ++i) {
        const int w = qMax(30, fm.horizontalAdvance(QLatin1String(kBtn[i])) + 14);
        r.btn[i] = QRect(x, r.strip.top() + 4, w, 19);
        x += w + 3;
    }
    r.srcLeft = x + 3;
    r.close = QRect(width() - 22, r.strip.top(), 20, kStripH);
    r.grip = QRect(width() - 42, r.strip.top(), 18, kStripH);

    const QString num = QStringLiteral("%1").arg(m_number, 2, 10, QLatin1Char('0'));
    const int wn = QFontMetrics(Theme::font(12, QFont::Bold)).horizontalAdvance(num);
    const int wt = QFontMetrics(Theme::font(12, QFont::DemiBold)).horizontalAdvance(m_ch->config().name);
    const int lw = qMin(width() - 16, wn + 7 + wt + 20);
    r.label = QRect((width() - lw) / 2, r.vid.bottom() - 6 - 20, lw, 21);
    return r;
}

bool ChannelTile::buttonEnabled(int i) const {
    switch (m_ch->state()) {
    case ChannelState::Preview: case ChannelState::Waiting: return i == 0;
    case ChannelState::Recording: case ChannelState::Reconnecting: return true;
    case ChannelState::Paused: return i != 3;
    default: return false;
    }
}

bool ChannelTile::buttonLit(int i) const {
    const ChannelState s = m_ch->state();
    return (i == 0 && (s == ChannelState::Recording || s == ChannelState::Reconnecting)) ||
           (i == 1 && s == ChannelState::Paused);
}

void ChannelTile::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const Rects r = rects();
    const ChannelState st = m_ch->state();
    const bool active = m_mgr->active() == m_ch;

    // --- obraz (object-fit: cover) ---
    p.fillRect(r.vid, Qt::black);
    const QImage& img = m_ch->frame();
    if (!img.isNull()) {
        const QSize crop = QSize(r.vid.size()).scaled(img.size(), Qt::KeepAspectRatio);
        const QRect src((img.width() - crop.width()) / 2, (img.height() - crop.height()) / 2, crop.width(), crop.height());
        p.setRenderHint(QPainter::SmoothPixmapTransform, r.vid.width() > img.width());
        p.drawImage(r.vid, img, src);
    }
    if (st == ChannelState::Error || st == ChannelState::Idle || st == ChannelState::Reconnecting) {
        p.fillRect(r.vid, QColor(0, 0, 0, 158));
        p.setPen(Qt::white);
        p.setFont(Theme::font(13, QFont::Bold, 1));
        const QString head = st == ChannelState::Error ? T("ERROR")
                           : st == ChannelState::Idle ? T("NO SIGNAL") : T("RECONNECTING");
        p.drawText(r.vid.adjusted(8, 0, -8, st == ChannelState::Error ? -16 : 0), Qt::AlignCenter, head);
        if (st == ChannelState::Error) {
            p.setFont(Theme::font(11));
            p.setPen(QColor(0xbb, 0xbb, 0xbb));
            p.drawText(r.vid.adjusted(12, 22, -12, 0), Qt::AlignCenter | Qt::TextWordWrap, m_ch->errorText());
        }
    }

    // --- ramka stanu ---
    QColor frame = Theme::idle;
    int fw = 2;
    if (st == ChannelState::Recording || st == ChannelState::Reconnecting || st == ChannelState::Error) { frame = Theme::red; fw = 3; }
    else if (st == ChannelState::Paused) { frame = Theme::amber; fw = 3; }
    else if (st == ChannelState::Preview) { frame = Theme::green; fw = 3; }
    p.setPen(QPen(frame, fw, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(Qt::NoBrush);
    const double h = fw / 2.0;
    p.drawRect(QRectF(r.vid).adjusted(h, h, -h, -h));
    if (active) {
        p.setPen(QPen(Qt::white, 1));
        p.drawRect(QRectF(r.vid).adjusted(3.5, 3.5, -3.5, -3.5));
    }

    // --- nakladki ---
    const QColor chip(0, 0, 0, 178);
    // pole zaznaczenia
    p.setPen(QPen(m_ch->isSelected() ? QColor(0xe6, 0xe6, 0xe6) : QColor(0xcf, 0xcf, 0xcf), 1.5));
    p.setBrush(m_ch->isSelected() ? QColor(0xe6, 0xe6, 0xe6) : QColor(0, 0, 0, 140));
    p.drawRect(r.selBox);
    if (m_ch->isSelected()) {
        p.setPen(Qt::black);
        p.setFont(Theme::font(10, QFont::Bold));
        p.drawText(r.selBox, Qt::AlignCenter, QStringLiteral("✓"));
    }
    // format
    p.setFont(Theme::font(10, QFont::Bold, 0.3));
    const QString fmt = m_ch->formatText();
    const QRect fr(30, 7, p.fontMetrics().horizontalAdvance(fmt) + 12, 15);
    p.fillRect(fr, chip);
    p.setPen(Qt::white);
    p.drawText(fr, Qt::AlignCenter, fmt);
    // miernik audio
    const QRectF am(width() - 8 - 12, 30, 12, r.vid.height() - 30 - 8);
    if (am.height() > 10) {
        Theme::paintMeter(p, QRectF(am.left(), am.top(), 5, am.height()), m_ch->level(0), true, QColor(0, 0, 0, 166));
        Theme::paintMeter(p, QRectF(am.left() + 7, am.top(), 5, am.height()), m_ch->level(1), true, QColor(0, 0, 0, 166));
    }
    // etykieta
    const QString num = QStringLiteral("%1").arg(m_number, 2, 10, QLatin1Char('0'));
    p.fillRect(r.label, QColor(0, 0, 0, 209));
    p.setFont(Theme::font(12, QFont::Bold));
    const int wn = p.fontMetrics().horizontalAdvance(num);
    p.setPen(QColor(0xbd, 0xbd, 0xbd));
    p.drawText(r.label.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, num);
    p.setFont(Theme::font(12, QFont::DemiBold));
    p.setPen(Qt::white);
    const QRect nameRect = r.label.adjusted(10 + wn + 7, 0, -10, 0);
    p.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(m_ch->config().name, Qt::ElideRight, nameRect.width()));

    // --- pasek ---
    p.fillRect(r.strip, m_ch->isSelected() ? QColor(0x3d, 0x3d, 0x3d) : QColor(0x2b, 0x2b, 0x2b));
    p.setPen(Qt::black);
    p.drawLine(r.strip.topLeft(), r.strip.topRight());
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < 4; ++i) {
        const bool en = buttonEnabled(i), lit = buttonLit(i);
        p.setOpacity(en ? 1.0 : 0.35);
        QColor back(0x3a, 0x3a, 0x3a), border(0x1b, 0x1b, 0x1b), text(0xcf, 0xcf, 0xcf);
        if (i == 0) text = QColor(0xff, 0x6d, 0x70);
        if (lit && i == 0) { back = Theme::red; border = QColor(0x8a, 0x14, 0x18); text = Qt::white; }
        if (lit && i == 1) { back = Theme::amber; border = QColor(0x8a, 0x5d, 0x0b); text = QColor(0x1a, 0x11, 0x00); }
        p.setPen(border);
        p.setBrush(back);
        p.drawRoundedRect(QRectF(r.btn[i]).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
        p.setPen(text);
        p.setFont(Theme::font(10, QFont::Bold, 0.5));
        p.drawText(r.btn[i], Qt::AlignCenter, QLatin1String(kBtn[i]));
    }
    p.setOpacity(1.0);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(Theme::font(12));
    p.setPen(Theme::t2);
    p.drawText(r.close, Qt::AlignCenter, QStringLiteral("✕"));
    p.setPen(Theme::t3);
    p.drawText(r.grip, Qt::AlignCenter, QStringLiteral("⋮⋮"));
    const QRect sr(r.srcLeft, r.strip.top(), r.grip.left() - r.srcLeft - 4, kStripH);
    if (sr.width() > 20) {
        p.setFont(Theme::font(10));
        p.setPen(Theme::t2);
        p.drawText(sr, Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(m_ch->sourceText(), Qt::ElideRight, sr.width()));
    }
}

void ChannelTile::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPoint pt = e->position().toPoint();
    const Rects r = rects();
    if (r.close.contains(pt)) { m_mgr->removeChannel(m_ch); return; }   // kafel jest usuwany przez deleteLater
    for (int i = 0; i < 4; ++i) {
        if (r.btn[i].contains(pt)) {
            if (buttonEnabled(i)) m_mgr->transport(m_ch, Transport(i));
            return;
        }
    }
    if (r.selBox.adjusted(-4, -4, 4, 4).contains(pt) || (e->modifiers() & Qt::ControlModifier)) {
        m_ch->setSelected(!m_ch->isSelected());
        return;
    }
    m_mgr->setActive(m_ch);
}

void ChannelTile::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!rects().label.contains(e->position().toPoint())) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, T("Channel name"), T("Name:"),
                                               QLineEdit::Normal, m_ch->config().name, &ok);
    if (ok) m_ch->setName(name);
}
