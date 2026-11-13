#include "ChannelList.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <functional>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include "ChannelTile.h"
#include "PreviewDialog.h"
#include "Theme.h"
#include "core/ChannelManager.h"
#include "core/Lang.h"

namespace {
const char* const kBtn[4] = {"REC", "II", "STOP", "SPLIT"};
const int kBtnW[4] = {38, 30, 44, 46};
const int HeaderH = 34;

QString stateText(const Channel* ch) {
    if (ch->signalLost() && ch->state() != ChannelState::Error) return T("RECONNECTING");
    switch (ch->state()) {
    case ChannelState::Recording: return QStringLiteral("REC");
    case ChannelState::Paused: return T("PAUSED");
    case ChannelState::Finalizing: return T("SAVING");
    case ChannelState::Preview: return ch->waitingForSchedule() ? T("WAITING") : QStringLiteral("PREVIEW");
    case ChannelState::Error: return T("ERROR");
    case ChannelState::Reconnecting: return T("RECONNECTING");
    default: return ch->inputOff() ? T("INPUT OFF") : T("NO SIGNAL");
    }
}

QColor stateColor(const Channel* ch) {
    switch (ch->state()) {
    case ChannelState::Recording: case ChannelState::Finalizing: case ChannelState::Error: return Theme::red;
    case ChannelState::Paused: return Theme::amber;
    case ChannelState::Preview: return ch->signalLost() ? Theme::amber : Theme::green;
    default: return Theme::t3;
    }
}

QRect headerButtonRect(const ListColumns& c, int i, int h) {
    int x = c.buttons;
    for (int k = 0; k < i; ++k) x += kBtnW[k] + 3;
    return QRect(x, (h - 22) / 2, kBtnW[i], 22);
}

// Naglowek kolumn - malowany tak samo liczonymi pozycjami co wiersze. Nad polami zaznaczenia ma pole
// "zaznacz wszystkie", a nad przyciskami wierszy - te same przyciski dzialajace na zaznaczone kanaly.
class HeaderRow : public QWidget {
public:
    HeaderRow(ChannelManager* mgr, QWidget* parent) : QWidget(parent), m_mgr(mgr) {
        connect(mgr, &ChannelManager::selectionChanged, this, qOverload<>(&QWidget::update));
        connect(mgr, &ChannelManager::listChanged, this, qOverload<>(&QWidget::update));
        setMouseTracking(true);
    }
    int thumbW = 64;
    std::function<void(Transport)> onTransport;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x26, 0x26, 0x26));
        p.setPen(QColor(0x15, 0x15, 0x15));
        p.drawLine(0, height() - 1, width(), height() - 1);
        const ListColumns c = ListColumns::forWidth(width(), thumbW);
        const int n = m_mgr->count(), sel = m_mgr->selectedCount();

        // zaznacz wszystkie: pelne = wszystkie, kreska = czesc, puste = zaden
        const QRect box(c.sel, (height() - 18) / 2, 18, 18);
        p.setRenderHint(QPainter::Antialiasing);
        const bool all = n > 0 && sel == n;
        p.setOpacity(n > 0 ? 1.0 : 0.35);
        p.setPen(QPen(all ? Qt::white : QColor(0xcf, 0xcf, 0xcf), 1.5));
        p.setBrush(all ? QColor(Qt::white) : QColor(0, 0, 0, 150));
        p.drawRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5));
        if (all) {
            p.setPen(QPen(Qt::black, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF o = box.topLeft();
            p.drawPolyline(QPolygonF() << o + QPointF(4, 9.5) << o + QPointF(7.5, 13) << o + QPointF(14, 5));
        } else if (sel > 0) {
            p.setPen(QPen(QColor(0xcf, 0xcf, 0xcf), 2.4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(box.left() + 4.5, box.center().y() + 0.5), QPointF(box.right() - 3.5, box.center().y() + 0.5));
        }
        p.setOpacity(1.0);
        p.setRenderHint(QPainter::Antialiasing, false);

        p.setFont(Theme::font(10, QFont::Bold, 0.4));
        p.setPen(Theme::t3);
        auto text = [&](int x, int x2, const QString& str) {
            p.drawText(QRect(x, 0, x2 - x - 6, height()), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(str.toUpper(), Qt::ElideRight, x2 - x - 6));
        };
        text(c.num, c.thumb, QStringLiteral("#"));
        text(c.name, c.source, T("Channel name"));
        text(c.source, c.format, T("Source"));
        text(c.format, c.state, T("Video format"));
        text(c.state, c.time, T("State"));
        text(c.time, c.rate, T("Time"));
        text(c.rate, c.live, QStringLiteral("Mb/s"));
        text(c.live, c.meter, T("Live output"));
        text(c.meter, c.buttons, QStringLiteral("Audio"));

        // przyciski dla zaznaczonych - wyglad jak w wierszach; REC swieci, gdy ktorys z zaznaczonych nagrywa
        bool selRec = false;
        for (const Channel* ch : m_mgr->selected()) selRec = selRec || ch->state() == ChannelState::Recording;
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < 4; ++i) {
            p.setOpacity(sel > 0 ? 1.0 : 0.35);
            QColor back(0x4a, 0x4a, 0x4a), border(0x12, 0x12, 0x12), fg(0xe6, 0xe6, 0xe6);
            if (i == 0) fg = QColor(0xff, 0x6d, 0x70);
            if (i == 0 && selRec) { back = Theme::red; border = QColor(0x8a, 0x14, 0x18); fg = Qt::white; }
            const QRect r = headerButtonRect(c, i, height());
            p.setPen(border);
            p.setBrush(back);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
            p.setPen(fg);
            p.setFont(Theme::font(10, QFont::Bold, 0.5));
            p.drawText(r, Qt::AlignCenter, QLatin1String(kBtn[i]));
        }
        p.setOpacity(1.0);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        const QPoint pt = e->position().toPoint();
        const ListColumns c = ListColumns::forWidth(width(), thumbW);
        if (pt.x() < c.num) {                   // zaznacz wszystkie / odznacz wszystkie
            if (m_mgr->count() > 0 && m_mgr->selectedCount() == m_mgr->count()) m_mgr->selectNone();
            else m_mgr->selectAll();
            return;
        }
        for (int i = 0; i < 4; ++i)
            if (headerButtonRect(c, i, height()).adjusted(0, -5, 0, 5).contains(pt)) {
                if (m_mgr->selectedCount() > 0 && onTransport) onTransport(Transport(i));
                return;
            }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const QPoint pt = e->position().toPoint();
        const ListColumns c = ListColumns::forWidth(width(), thumbW);
        QString tip;
        if (pt.x() < c.num) tip = m_mgr->count() > 0 && m_mgr->selectedCount() == m_mgr->count() ? T("NONE") : T("ALL");
        else if (pt.x() >= c.buttons && pt.x() < c.close) tip = T("Selected channels (%1)").arg(m_mgr->selectedCount());
        setToolTip(tip);
    }

private:
    ChannelManager* m_mgr;
};
}  // namespace

// Kolumny o stalej szerokosci stoja po bokach, nazwa i zrodlo dziela reszte (40% / 60%).
ListColumns ListColumns::forWidth(int w, int thumbW) {
    ListColumns c{};
    c.sel = 10;
    c.num = 38;
    c.thumb = 66;
    c.name = c.thumb + thumbW + 12;
    c.end = w;
    c.close = w - 28;
    c.buttons = c.close - (kBtnW[0] + kBtnW[1] + kBtnW[2] + kBtnW[3] + 3 * 3) - 8;
    c.meter = c.buttons - 110;
    c.live = c.meter - 116;
    c.rate = c.live - 56;
    c.time = c.rate - 92;
    c.state = c.time - 104;
    c.format = c.state - 88;
    const int flex = qMax(120, c.format - c.name);
    c.source = c.name + flex / 2;
    return c;
}

// ---------------------------------------------------------------- lista

ChannelList::ChannelList(ChannelManager* mgr, QWidget* parent) : QScrollArea(parent), m_mgr(mgr) {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet(QStringLiteral("ChannelList, #listInner { background:#0b0b0b; }"));
    m_inner = new QWidget;
    m_inner->setObjectName(QStringLiteral("listInner"));
    setWidget(m_inner);
    auto* header = new HeaderRow(mgr, m_inner);
    header->onTransport = [this](Transport t) { emit transportSelectedRequested(t); };
    m_header = header;
    m_hint = new QLabel(T("No channels") + QStringLiteral("\n\n") + T("ADD SOURCE adds a channel (max. 16)"), m_inner);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setStyleSheet(QStringLiteral("color:#6e6e6e; background:transparent; font-size:14px;"));
    connect(mgr, &ChannelManager::listChanged, this, &ChannelList::rebuild);
    rebuild();
}

void ChannelList::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    relayout();
}

void ChannelList::rebuild() {
    if (m_marker) m_marker->hide();
    m_dragGap = -1;
    for (ChannelRow* r : m_rows) {
        r->hide();
        r->deleteLater();
    }
    m_rows.clear();
    int n = 0;
    for (Channel* ch : m_mgr->channels()) {
        auto* r = new ChannelRow(m_mgr, ch, ++n, m_inner);
        connect(r, &ChannelRow::dragMoved, this, [this, r](const QPoint& p) { dragMove(r, p); });
        connect(r, &ChannelRow::dragFinished, this, [this, r](const QPoint& p) { dragEnd(r, p); });
        r->show();
        m_rows.append(r);
    }
    m_hint->setVisible(m_rows.isEmpty());
    relayout();
}

void ChannelList::relayout() {
    const QSize vp = viewport()->size();
    const int avail = qMax(0, vp.height() - HeaderH);
    const int n = int(m_rows.size());
    // 16 kanalow ma sie zmiescic bez przewijania; mniej kanalow = wyzsze wiersze i wieksze miniatury
    const int rowH = qBound(ChannelRow::MinRowH, avail / qMax(n, 1), ChannelRow::MaxRowH);
    const int fit16 = qMax(ChannelRow::MinRowH, avail / ChannelManager::MaxChannels);
    const int h = n > 0 ? qMax(rowH, qMin(fit16, ChannelRow::MaxRowH)) : rowH;
    static_cast<HeaderRow*>(m_header)->thumbW = ChannelRow::thumbWidthFor(h);
    m_inner->resize(vp.width(), qMax(vp.height(), HeaderH + n * h));
    m_header->setGeometry(0, 0, vp.width(), HeaderH);
    m_header->update();
    m_hint->setGeometry(0, HeaderH, vp.width(), avail);
    for (int i = 0; i < n; ++i) m_rows[i]->setGeometry(0, HeaderH + i * h, vp.width(), h);
    m_rowH = h;
}

void ChannelList::dragMove(ChannelRow* row, const QPoint& globalPos) {
    const int from = int(m_rows.indexOf(row)), n = int(m_rows.size());
    if (from < 0) return;
    if (!m_marker) {
        m_marker = new QWidget(m_inner);
        m_marker->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_marker->setAttribute(Qt::WA_StyledBackground);
        m_marker->setStyleSheet(QStringLiteral("background:#ff8a1f;"));
    }
    const QPoint pos = m_inner->mapFromGlobal(globalPos);
    const bool inside = viewport()->rect().contains(viewport()->mapFromGlobal(globalPos));
    const int gap = qBound(0, (pos.y() - HeaderH + m_rowH / 2) / m_rowH, n);
    // upuszczenie tuz nad lub tuz pod samym soba niczego nie zmienia; poza lista - anulowanie
    m_dragGap = (!inside || gap == from || gap == from + 1) ? -1 : gap;
    if (m_dragGap < 0) {
        m_marker->hide();
        return;
    }
    m_marker->setGeometry(0, HeaderH + m_dragGap * m_rowH - 2, m_inner->width(), 4);
    m_marker->show();
    m_marker->raise();
}

void ChannelList::dragEnd(ChannelRow* row, const QPoint& globalPos) {
    dragMove(row, globalPos);
    const int from = int(m_rows.indexOf(row)), gap = m_dragGap;
    if (m_marker) m_marker->hide();
    m_dragGap = -1;
    if (from >= 0 && gap >= 0) m_mgr->moveChannel(from, gap);      // przebudowuje liste
}

// ---------------------------------------------------------------- wiersz

ChannelRow::ChannelRow(ChannelManager* mgr, Channel* ch, int number, QWidget* parent)
    : QWidget(parent), m_mgr(mgr), m_ch(ch), m_number(number) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(ch, &Channel::frameChanged, this, qOverload<>(&QWidget::update));
    connect(ch, &Channel::levelsChanged, this, qOverload<>(&QWidget::update));
    connect(ch, &Channel::changed, this, qOverload<>(&QWidget::update));
    connect(mgr, &ChannelManager::activeChanged, this, qOverload<>(&QWidget::update));
}

QRect ChannelRow::buttonRect(const ListColumns& c, int i) const {
    int x = c.buttons;
    for (int k = 0; k < i; ++k) x += kBtnW[k] + 3;
    return QRect(x, (height() - 22) / 2, kBtnW[i], 22);
}

void ChannelRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const ListColumns c = ListColumns::forWidth(width(), thumbWidthFor(height()));
    const bool active = m_mgr->active() == m_ch;
    const bool selected = m_ch->isSelected();
    const ChannelState st = m_ch->state();

    p.fillRect(rect(), selected ? QColor(0x33, 0x33, 0x33) : (m_number % 2 ? QColor(0x1c, 0x1c, 0x1c) : QColor(0x20, 0x20, 0x20)));
    p.setPen(QColor(0x0b, 0x0b, 0x0b));
    p.drawLine(0, height() - 1, width(), height() - 1);
    if (active) p.fillRect(QRect(0, 0, 4, height() - 1), Theme::orange);      // kanal aktywny: pomaranczowy pasek z lewej

    // pole zaznaczenia - jak na kaflu
    const QRect box(c.sel, (height() - 18) / 2, 18, 18);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(selected ? Qt::white : QColor(0xcf, 0xcf, 0xcf), 1.5));
    p.setBrush(selected ? QColor(Qt::white) : QColor(0, 0, 0, 150));
    p.drawRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5));
    if (selected) {
        p.setPen(QPen(Qt::black, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF o = box.topLeft();
        p.drawPolyline(QPolygonF() << o + QPointF(4, 9.5) << o + QPointF(7.5, 13) << o + QPointF(14, 5));
    }
    p.setRenderHint(QPainter::Antialiasing, false);

    auto text = [&](int x, int x2, const QString& s, const QColor& color, const QFont& f) {
        p.setFont(f);
        p.setPen(color);
        const QRect r(x, 0, x2 - x - 8, height() - 1);
        p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(s, Qt::ElideRight, r.width()));
    };
    text(c.num, c.thumb, QStringLiteral("%1").arg(m_number, 2, 10, QLatin1Char('0')), active ? Theme::orange : Theme::t2, Theme::font(12, QFont::Bold));

    // miniatura 16:9 z ramka stanu
    const int thH = height() - 8, thW = thumbWidthFor(height());
    const QRect th(c.thumb, 4, thW, thH - 1);
    p.fillRect(th, Qt::black);
    if (!m_ch->frame().isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(th, m_ch->frame());
    }
    p.setPen(QPen(stateColor(m_ch), 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(th).adjusted(1, 1, -1, -1));

    text(c.name, c.source, m_ch->config().name, active ? QColor(Qt::white) : Theme::t1, Theme::font(13, active ? QFont::Bold : QFont::DemiBold));
    text(c.source, c.format, m_ch->sourceText(), Theme::t2, Theme::font(11));
    text(c.format, c.state, m_ch->formatText(), Theme::t2, Theme::font(11));
    // stan: kropka + napis
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(stateColor(m_ch));
    p.drawEllipse(QRectF(c.state, height() / 2.0 - 4.5, 8, 8));
    p.setRenderHint(QPainter::Antialiasing, false);
    text(c.state + 14, c.time, stateText(m_ch), st == ChannelState::Preview && !m_ch->signalLost() ? Theme::t2 : Theme::t1, Theme::font(10, QFont::Bold, 0.3));
    if (m_ch->isRecording()) {
        const qint64 sec = m_ch->recordMs() / 1000;
        QString t = QStringLiteral("%1:%2:%3").arg(sec / 3600, 2, 10, QLatin1Char('0')).arg(sec / 60 % 60, 2, 10, QLatin1Char('0'))
                        .arg(sec % 60, 2, 10, QLatin1Char('0'));
        if (m_ch->recordPart() > 1) t += QStringLiteral(" · %1").arg(m_ch->recordPart());
        text(c.time, c.rate, t, QColor(Qt::white), Theme::font(12, QFont::DemiBold));
        text(c.rate, c.live, QString::number(m_ch->recordMbps(), 'f', 1), Theme::t1, Theme::font(11));
    }
    if (m_ch->liveOn()) {
        p.setFont(Theme::font(10, QFont::Bold, 0.3));
        const QString lt = p.fontMetrics().elidedText(m_ch->liveText(), Qt::ElideRight, c.meter - c.live - 22);
        const QRect lr(c.live, (height() - 17) / 2, p.fontMetrics().horizontalAdvance(lt) + 12, 17);
        // szary: awaria wyjscia; ciemny: wyjscie czeka na okno harmonogramu
        p.fillRect(lr, m_ch->liveFailed() ? QColor(0x55, 0x55, 0x55) : m_ch->liveActive() ? QColor(0x1d, 0x6f, 0xd6) : QColor(0x24, 0x3a, 0x57));
        p.setPen(m_ch->liveActive() ? QColor(Qt::white) : QColor(0x9d, 0x9d, 0x9d));
        p.drawText(lr, Qt::AlignCenter, lt);
    }
    // mierniki L / R
    // ikona odsluchu obok miernikow
    Theme::paintHeadphones(p, QRectF(c.meter, height() / 2.0 - 8, 18, 15), m_ch->monitorOn());
    const int mw = c.buttons - c.meter - 14 - 24;
    Theme::paintMeter(p, QRectF(c.meter + 24, height() / 2.0 - 7, mw, 5), m_ch->level(0), false, QColor(0x11, 0x11, 0x11));
    Theme::paintMeter(p, QRectF(c.meter + 24, height() / 2.0 + 1, mw, 5), m_ch->level(1), false, QColor(0x11, 0x11, 0x11));

    // przyciski transportu - te same reguly co na kaflu
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < 4; ++i) {
        const bool en = ChannelTile::transportEnabled(st, i), lit = ChannelTile::transportLit(st, i);
        p.setOpacity(en || lit ? 1.0 : 0.35);
        QColor back(0x3a, 0x3a, 0x3a), border(0x1b, 0x1b, 0x1b), fg(0xcf, 0xcf, 0xcf);
        if (i == 0) fg = QColor(0xff, 0x6d, 0x70);
        if (lit && i == 0) { back = Theme::red; border = QColor(0x8a, 0x14, 0x18); fg = Qt::white; }
        if (lit && i == 1) { back = Theme::amber; border = QColor(0x8a, 0x5d, 0x0b); fg = QColor(0x1a, 0x11, 0x00); }
        const QRect r = buttonRect(c, i);
        p.setPen(border);
        p.setBrush(back);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
        p.setPen(fg);
        p.setFont(Theme::font(10, QFont::Bold, 0.5));
        p.drawText(r, Qt::AlignCenter, QLatin1String(kBtn[i]));
    }
    p.setOpacity(1.0);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(Theme::font(12));
    p.setPen(Theme::t2);
    p.drawText(QRect(c.close, 0, 24, height()), Qt::AlignCenter, QStringLiteral("✕"));
}

void ChannelRow::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPoint pt = e->position().toPoint();
    const ListColumns c = ListColumns::forWidth(width(), thumbWidthFor(height()));
    if (pt.x() >= c.close) {
        if (ChannelTile::confirmRemove(this, {m_ch})) m_mgr->removeChannel(m_ch);
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (buttonRect(c, i).adjusted(0, -4, 0, 4).contains(pt)) {
            if (ChannelTile::transportEnabled(m_ch->state(), i)) m_mgr->transport(m_ch, Transport(i));
            return;
        }
    }
    if (pt.x() >= c.meter - 4 && pt.x() < c.meter + 22) {       // sluchawki = odsluch kanalu
        m_mgr->setMonitored(m_ch);
        return;
    }
    if (pt.x() < c.num || (e->modifiers() & Qt::ControlModifier)) {
        m_ch->setSelected(!m_ch->isSelected());
        return;
    }
    m_pressPos = pt;
    m_pressed = true;
    m_mgr->setActive(m_ch);
}

void ChannelRow::mouseMoveEvent(QMouseEvent* e) {
    if (!m_pressed || !(e->buttons() & Qt::LeftButton)) return;
    if (!m_dragging) {
        if ((e->position().toPoint() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) return;
        m_dragging = true;
        setCursor(Qt::ClosedHandCursor);
    }
    emit dragMoved(e->globalPosition().toPoint());
}

void ChannelRow::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const bool was = m_dragging;
    m_pressed = m_dragging = false;
    if (!was) return;
    unsetCursor();
    emit dragFinished(e->globalPosition().toPoint());      // po tym sygnale wiersz moze juz nie istniec
}

void ChannelRow::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_pressed = false;
    const int x = e->position().toPoint().x();
    const ListColumns c = ListColumns::forWidth(width(), thumbWidthFor(height()));
    if (x >= c.thumb && x < c.name) {                 // miniatura = duzy podglad
        PreviewDialog::open(m_mgr, m_ch, m_number, window());
    } else if (x >= c.name && x < c.source) {         // nazwa = zmiana nazwy
        bool ok = false;
        const QString name = QInputDialog::getText(this, T("Channel name"), T("Name:"), QLineEdit::Normal, m_ch->config().name, &ok);
        if (ok) m_ch->setName(name);
    }
}

// To samo menu co na kaflu: na wierszu zaznaczonym dziala na wszystkie zaznaczone.
void ChannelRow::contextMenuEvent(QContextMenuEvent* e) {
    const QList<Channel*> sel = m_mgr->selected();
    const QList<Channel*> targets = m_ch->isSelected() && sel.size() > 1 ? sel : QList<Channel*>{m_ch};
    const bool many = targets.size() > 1;
    ChannelManager* mgr = m_mgr;
    QWidget* top = window();
    QMenu menu(this);
    QAction* dup = menu.addAction(many ? T("Duplicate selected (%1)").arg(targets.size()) : T("Duplicate"));
    dup->setEnabled(mgr->canAdd());
    QAction* rem = menu.addAction(many ? T("Remove selected (%1)").arg(targets.size()) : T("Remove"));
    QAction* chosen = menu.exec(e->globalPos());
    if (chosen == dup) mgr->duplicateChannels(targets);
    else if (chosen == rem && ChannelTile::confirmRemove(top, targets)) mgr->removeChannels(targets);
}
