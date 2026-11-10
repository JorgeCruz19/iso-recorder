#include "ChannelTile.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>

#include "PreviewDialog.h"
#include "Theme.h"
#include "core/ChannelManager.h"
#include "core/Lang.h"

namespace {
const char* const kBtn[4] = {"REC", "II", "STOP", "SPLIT"};
}

ChannelTile::ChannelTile(ChannelManager* mgr, Channel* ch, int number, QWidget* parent)
    : QWidget(parent), m_mgr(mgr), m_ch(ch), m_number(number) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(ch, &Channel::frameChanged, this, qOverload<>(&QWidget::update));
    connect(ch, &Channel::levelsChanged, this, qOverload<>(&QWidget::update));
    connect(ch, &Channel::changed, this, qOverload<>(&QWidget::update));
    connect(mgr, &ChannelManager::activeChanged, this, qOverload<>(&QWidget::update));
}

// Wysokosc kafla dla danej szerokosci: kadr wideo jest zawsze 16:9.
int ChannelTile::heightForTileWidth(int w) {
    return (w - 2 * Margin) * 9 / 16 + StripH + 2 * Margin;
}

int ChannelTile::tileWidthForHeight(int h) {
    return (h - StripH - 2 * Margin) * 16 / 9 + 2 * Margin;
}

ChannelTile::Rects ChannelTile::rects() const {
    Rects r;
    const QRect in = rect().adjusted(Margin, Margin, -Margin, -Margin);
    r.vid = QRect(in.left(), in.top(), in.width(), in.height() - StripH);
    r.strip = QRect(in.left(), r.vid.bottom() + 1, in.width(), StripH);
    r.selBox = QRect(in.left() + 8, in.top() + 8, 18, 18);
    const QFontMetrics fm(Theme::font(10, QFont::Bold, 0.5));
    int x = in.left() + 4;
    for (int i = 0; i < 4; ++i) {
        const int w = qMax(30, fm.horizontalAdvance(QLatin1String(kBtn[i])) + 14);
        r.btn[i] = QRect(x, r.strip.top() + 4, w, 19);
        x += w + 3;
    }
    r.srcLeft = x + 3;
    r.close = QRect(in.right() - 21, r.strip.top(), 20, StripH);
    r.grip = QRect(in.right() - 41, r.strip.top(), 18, StripH);

    const QString num = QStringLiteral("%1").arg(m_number, 2, 10, QLatin1Char('0'));
    const int wn = QFontMetrics(Theme::font(12, QFont::Bold)).horizontalAdvance(num);
    // szerokosc liczona dla pogrubienia (etykieta kanalu aktywnego jest pogrubiona)
    const int wt = QFontMetrics(Theme::font(12, QFont::Bold)).horizontalAdvance(m_ch->config().name);
    const int lw = qMin(in.width() - 16, wn + 7 + wt + 22);
    r.label = QRect(in.left() + (in.width() - lw) / 2, r.vid.bottom() - 6 - 20, lw, 21);
    return r;
}

bool ChannelTile::buttonEnabled(int i) const { return transportEnabled(m_ch->state(), i); }
bool ChannelTile::buttonLit(int i) const { return transportLit(m_ch->state(), i); }

bool ChannelTile::transportEnabled(ChannelState s, int i) {
    switch (s) {
    case ChannelState::Preview: case ChannelState::Waiting: return i == 0;
    case ChannelState::Recording: case ChannelState::Reconnecting: return i >= 2;   // STOP i SPLIT; pauza w kolejnej wersji
    case ChannelState::Paused: return i != 3;
    default: return false;
    }
}

bool ChannelTile::transportLit(ChannelState s, int i) {
    return (i == 0 && (s == ChannelState::Recording || s == ChannelState::Reconnecting || s == ChannelState::Finalizing)) ||
           (i == 1 && s == ChannelState::Paused);
}

void ChannelTile::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const Rects r = rects();
    const ChannelState st = m_ch->state();
    const bool active = m_mgr->active() == m_ch;
    const bool selected = m_ch->isSelected();

    // --- tlo i ramka kanalu aktywnego: gruba, pomaranczowa, wokol calego kafla ---
    p.fillRect(rect(), QColor(0x0b, 0x0b, 0x0b));
    if (active) {
        p.setPen(QPen(Theme::orange, 3, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
    }

    // --- obraz: klatka podgladu ma zawsze 16:9 (inne proporcje sa juz wpisane z czarnymi pasami) ---
    p.fillRect(r.vid, Qt::black);
    const QImage& img = m_ch->frame();
    if (!img.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, r.vid.width() != img.width());
        p.drawImage(r.vid, img);
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

    // --- ramka stanu (wewnatrz kadru) ---
    QColor frame = Theme::idle;
    int fw = 2;
    if (st == ChannelState::Recording || st == ChannelState::Reconnecting || st == ChannelState::Error ||
        st == ChannelState::Finalizing) { frame = Theme::red; fw = 3; }
    else if (st == ChannelState::Paused) { frame = Theme::amber; fw = 3; }
    else if (st == ChannelState::Preview) { frame = Theme::green; fw = 3; }
    p.setPen(QPen(frame, fw, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(Qt::NoBrush);
    const double h = fw / 2.0;
    p.drawRect(QRectF(r.vid).adjusted(h, h, -h, -h));

    // --- nakladki ---
    const QColor chip(0, 0, 0, 178);
    // pole zaznaczenia: zaznaczone = biale, pelne, z czarnym znakiem
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(selected ? Qt::white : QColor(0xcf, 0xcf, 0xcf), 1.5));
    p.setBrush(selected ? QColor(Qt::white) : QColor(0, 0, 0, 150));
    p.drawRect(QRectF(r.selBox).adjusted(0.5, 0.5, -0.5, -0.5));
    if (selected) {
        p.setPen(QPen(Qt::black, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF o = r.selBox.topLeft();
        p.drawPolyline(QPolygonF() << o + QPointF(4, 9.5) << o + QPointF(7.5, 13) << o + QPointF(14, 5));
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    // format
    p.setFont(Theme::font(10, QFont::Bold, 0.3));
    const QString fmt = m_ch->formatText();
    const QRect fr(r.selBox.right() + 7, r.vid.top() + 9, p.fontMetrics().horizontalAdvance(fmt) + 12, 15);
    p.fillRect(fr, chip);
    p.setPen(Qt::white);
    p.drawText(fr, Qt::AlignCenter, fmt);
    // czas nagrania z kropka stanu (prawy gorny rog)
    if (m_ch->isRecording()) {
        const qint64 sec = m_ch->recordMs() / 1000;
        QString t = st == ChannelState::Finalizing ? T("SAVING")
            : QStringLiteral("%1:%2:%3").arg(sec / 3600, 2, 10, QLatin1Char('0')).arg(sec / 60 % 60, 2, 10, QLatin1Char('0'))
                                         .arg(sec % 60, 2, 10, QLatin1Char('0'));
        if (st != ChannelState::Finalizing && m_ch->recordPart() > 1)      // numer czesci po splicie
            t += QStringLiteral("  ·  %1").arg(m_ch->recordPart());
        p.setFont(Theme::font(11, QFont::Bold));
        const int tw = p.fontMetrics().horizontalAdvance(t);
        const QRect rr(r.vid.right() - 8 - (tw + 24), r.vid.top() + 8, tw + 24, 17);
        p.fillRect(rr, chip);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::red);
        p.drawEllipse(QRectF(rr.left() + 6, rr.top() + 5, 7, 7));
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(Qt::white);
        p.drawText(rr.adjusted(18, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, t);
    }
    // miernik audio
    const QRectF am(r.vid.right() - 8 - 11, r.vid.top() + 30, 12, r.vid.height() - 30 - 8);
    if (am.height() > 10) {
        Theme::paintMeter(p, QRectF(am.left(), am.top(), 5, am.height()), m_ch->level(0), true, QColor(0, 0, 0, 166));
        Theme::paintMeter(p, QRectF(am.left() + 7, am.top(), 5, am.height()), m_ch->level(1), true, QColor(0, 0, 0, 166));
    }
    // etykieta: na kanale aktywnym pomaranczowa z ciemnym tekstem
    const QString num = QStringLiteral("%1").arg(m_number, 2, 10, QLatin1Char('0'));
    p.fillRect(r.label, active ? Theme::orange : QColor(0, 0, 0, 209));
    p.setFont(Theme::font(12, QFont::Bold));
    const int wn = p.fontMetrics().horizontalAdvance(num);
    p.setPen(active ? QColor(0x3a, 0x1c, 0x00) : QColor(0xbd, 0xbd, 0xbd));
    p.drawText(r.label.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, num);
    p.setFont(Theme::font(12, active ? QFont::Bold : QFont::DemiBold));
    p.setPen(active ? QColor(0x14, 0x0a, 0x00) : QColor(Qt::white));
    const QRect nameRect = r.label.adjusted(10 + wn + 7, 0, -10, 0);
    p.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(m_ch->config().name, Qt::ElideRight, nameRect.width()));

    // --- pasek: jasniejszy na kanalach zaznaczonych ---
    p.fillRect(r.strip, selected ? QColor(0x4e, 0x4e, 0x4e) : QColor(0x2b, 0x2b, 0x2b));
    p.setPen(Qt::black);
    p.drawLine(r.strip.topLeft(), r.strip.topRight());
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < 4; ++i) {
        const bool en = buttonEnabled(i), lit = buttonLit(i);
        p.setOpacity(en || lit ? 1.0 : 0.35);
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
    p.setPen(selected ? Theme::t1 : Theme::t2);
    p.drawText(r.close, Qt::AlignCenter, QStringLiteral("✕"));
    p.setPen(selected ? Theme::t2 : Theme::t3);
    p.drawText(r.grip, Qt::AlignCenter, QStringLiteral("⋮⋮"));
    const QRect sr(r.srcLeft, r.strip.top(), r.grip.left() - r.srcLeft - 4, StripH);
    if (sr.width() > 20) {
        p.setFont(Theme::font(10));
        p.setPen(selected ? Theme::t1 : Theme::t2);
        p.drawText(sr, Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(m_ch->sourceText(), Qt::ElideRight, sr.width()));
    }
}

void ChannelTile::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPoint pt = e->position().toPoint();
    const Rects r = rects();
    if (r.close.contains(pt)) {      // kafel jest usuwany przez deleteLater
        if (confirmRemove(this, {m_ch})) m_mgr->removeChannel(m_ch);
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (r.btn[i].contains(pt)) {
            if (buttonEnabled(i)) m_mgr->transport(m_ch, Transport(i));
            return;
        }
    }
    if (r.selBox.adjusted(-5, -5, 5, 5).contains(pt) || (e->modifiers() & Qt::ControlModifier)) {
        m_ch->setSelected(!m_ch->isSelected());
        return;
    }
    m_pressPos = pt;        // od tego miejsca mozna zaczac przeciaganie kafla
    m_pressed = true;
    m_mgr->setActive(m_ch);
}

void ChannelTile::mouseMoveEvent(QMouseEvent* e) {
    if (!m_pressed || !(e->buttons() & Qt::LeftButton)) return;
    if (!m_dragging) {
        if ((e->position().toPoint() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) return;
        m_dragging = true;
        setCursor(Qt::ClosedHandCursor);
        emit dragStarted();
    }
    emit dragMoved(e->globalPosition().toPoint());
}

void ChannelTile::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const bool was = m_dragging;
    m_pressed = m_dragging = false;
    if (!was) return;
    unsetCursor();
    emit dragFinished(e->globalPosition().toPoint());      // po tym sygnale kafel moze juz nie istniec
}

void ChannelTile::mouseDoubleClickEvent(QMouseEvent* e) {
    const Rects r = rects();
    const QPoint pt = e->position().toPoint();
    m_pressed = false;
    if (!r.label.contains(pt)) {        // dwuklik w obraz (poza polem zaznaczenia) = duzy podglad
        if (r.vid.contains(pt) && !r.selBox.adjusted(-5, -5, 5, 5).contains(pt) && e->button() == Qt::LeftButton)
            PreviewDialog::open(m_mgr, m_ch, m_number, window());
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, T("Channel name"), T("Name:"),
                                               QLineEdit::Normal, m_ch->config().name, &ok);
    if (ok) m_ch->setName(name);
}

bool ChannelTile::confirmRemove(QWidget* parent, const QList<Channel*>& list) {
    if (list.isEmpty()) return false;
    bool recording = false;
    for (Channel* c : list) recording = recording || c->isRecording();
    if (list.size() == 1 && !recording) return true;
    QString text = list.size() == 1 ? T("Remove channel %1?").arg(list.first()->config().name)
                                    : T("Remove selected channels (%1)?").arg(list.size());
    if (recording) text += QStringLiteral("\n\n") + T("Recordings in progress will be finished and saved first.");
    return QMessageBox::question(parent, QStringLiteral("ISO Recorder"), text) == QMessageBox::Yes;
}

// Menu kontekstowe: na kaflu zaznaczonym dziala na wszystkie zaznaczone, na niezaznaczonym - tylko na ten kanal.
void ChannelTile::contextMenuEvent(QContextMenuEvent* e) {
    const QList<Channel*> sel = m_mgr->selected();
    const QList<Channel*> targets = m_ch->isSelected() && sel.size() > 1 ? sel : QList<Channel*>{m_ch};
    const bool many = targets.size() > 1;
    ChannelManager* mgr = m_mgr;      // kafel moze zniknac, zanim akcja sie skonczy
    QWidget* top = window();
    QMenu menu(this);
    QAction* dup = menu.addAction(many ? T("Duplicate selected (%1)").arg(targets.size()) : T("Duplicate"));
    dup->setEnabled(mgr->canAdd());
    QAction* rem = menu.addAction(many ? T("Remove selected (%1)").arg(targets.size()) : T("Remove"));
    QAction* chosen = menu.exec(e->globalPos());
    if (chosen == dup) mgr->duplicateChannels(targets);
    else if (chosen == rem && confirmRemove(top, targets)) mgr->removeChannels(targets);
}
