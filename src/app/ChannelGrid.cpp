#include "ChannelGrid.h"

#include <QLabel>
#include <QPainter>
#include <QScrollBar>

#include "ChannelTile.h"
#include "core/ChannelManager.h"
#include "core/Lang.h"

ChannelGrid::ChannelGrid(ChannelManager* mgr, QWidget* parent) : QScrollArea(parent), m_mgr(mgr) {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet(QStringLiteral("ChannelGrid, #gridInner { background:#0b0b0b; }"));
    m_inner = new QWidget;
    m_inner->setObjectName(QStringLiteral("gridInner"));
    setWidget(m_inner);
    m_hint = new QLabel(T("No channels") + QStringLiteral("\n\n") + T("ADD SOURCE adds a channel (max. 16)"), m_inner);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setStyleSheet(QStringLiteral("color:#6e6e6e; background:transparent; font-size:14px;"));
    connect(mgr, &ChannelManager::listChanged, this, &ChannelGrid::rebuild);
    rebuild();
}

void ChannelGrid::setColumns(int n) {
    m_cols = n;
    relayout();
}

void ChannelGrid::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    relayout();
}

void ChannelGrid::rebuild() {
    dragClear();        // lista zmienila sie w trakcie przeciagania
    for (ChannelTile* t : m_tiles) {
        t->hide();
        t->deleteLater();
    }
    m_tiles.clear();
    int n = 0;
    for (Channel* ch : m_mgr->channels()) {
        auto* t = new ChannelTile(m_mgr, ch, ++n, m_inner);
        connect(t, &ChannelTile::dragStarted, this, [this, t] { dragBegin(t); });
        connect(t, &ChannelTile::dragMoved, this, &ChannelGrid::dragMove);
        connect(t, &ChannelTile::dragFinished, this, &ChannelGrid::dragEnd);
        t->show();
        m_tiles.append(t);
    }
    m_hint->setVisible(m_tiles.isEmpty());
    relayout();
}

int ChannelGrid::widthForHeight(int h, int cols) {
    return cols * ChannelTile::tileWidthForHeight((h - 2 * Pad) / cols) + 2 * Pad;
}

void ChannelGrid::relayout() {
    // Kadr kafla ma zawsze 16:9, wiec rozmiar kafla wynika z ciasniejszego z dwoch wymiarow.
    // Siatka cols x cols jest wysrodkowana w obu osiach - marginesy z lewej/prawej i z gory/dolu sa rowne.
    const QSize vp = viewport()->size();
    const int availW = (vp.width() - 2 * Pad) / m_cols;
    const int availH = (vp.height() - 2 * Pad) / m_cols;
    const int tw = qMax(120, qMin(availW, ChannelTile::tileWidthForHeight(availH)));
    const int th = ChannelTile::heightForTileWidth(tw);
    const int rows = (int(m_tiles.size()) + m_cols - 1) / m_cols;
    const int x0 = qMax(Pad, (vp.width() - m_cols * tw) / 2);
    const int y0 = qMax(Pad, (vp.height() - m_cols * th) / 2);
    m_inner->resize(vp.width(), qMax(vp.height(), 2 * y0 + rows * th));
    m_x0 = x0; m_y0 = y0; m_tw = tw; m_th = th;
    m_hint->setGeometry(m_inner->rect());
    for (int i = 0; i < m_tiles.size(); ++i)
        m_tiles[i]->setGeometry(x0 + (i % m_cols) * tw, y0 + (i / m_cols) * th, tw, th);
}

void ChannelGrid::dragBegin(ChannelTile* tile) {
    dragClear();
    m_dragFrom = int(m_tiles.indexOf(tile));
    if (m_dragFrom < 0) return;
    const QPixmap shot = tile->grab();
    QPixmap faded(shot.size());
    faded.setDevicePixelRatio(shot.devicePixelRatio());
    faded.fill(Qt::transparent);
    {
        QPainter p(&faded);
        p.setOpacity(0.7);
        p.drawPixmap(0, 0, shot);
    }
    m_ghost = new QLabel(m_inner);
    m_ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_ghost->setStyleSheet(QStringLiteral("background:transparent;"));
    m_ghost->setPixmap(faded);
    m_ghost->resize(tile->size());
    m_marker = new QWidget(m_inner);
    m_marker->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_marker->setAttribute(Qt::WA_StyledBackground);
    m_marker->setStyleSheet(QStringLiteral("background:#ff8a1f;"));
    m_marker->hide();
}

// Pozycja wstawienia (0..n) dla punktu w ukladzie m_inner: blizsza krawedz kafla pod kursorem.
int ChannelGrid::gapAt(const QPoint& pos) const {
    const int n = int(m_tiles.size());
    const int rows = (n + m_cols - 1) / m_cols;
    if (rows == 0) return 0;
    const int row = qBound(0, (pos.y() - m_y0) / m_th, rows - 1);
    const int col = qBound(0, (pos.x() - m_x0 + m_tw / 2) / m_tw, m_cols);
    return qMin(row * m_cols + col, n);
}

void ChannelGrid::dragMove(const QPoint& globalPos) {
    if (m_dragFrom < 0 || !m_ghost) return;
    // przewijanie, gdy kursor jest przy gornej lub dolnej krawedzi
    const QPoint vp = viewport()->mapFromGlobal(globalPos);
    if (vp.y() < 24) verticalScrollBar()->setValue(verticalScrollBar()->value() - 16);
    else if (vp.y() > viewport()->height() - 24) verticalScrollBar()->setValue(verticalScrollBar()->value() + 16);

    const QPoint pos = m_inner->mapFromGlobal(globalPos);
    m_ghost->move(pos - QPoint(m_ghost->width() / 2, m_ghost->height() / 2));
    m_ghost->show();

    const int gap = viewport()->rect().contains(vp) ? gapAt(pos) : -1;
    // upuszczenie tuz przed lub tuz za samym soba niczego nie zmienia - bez znacznika
    m_dragGap = (gap == m_dragFrom || gap == m_dragFrom + 1) ? -1 : gap;
    if (m_dragGap < 0) {
        m_marker->hide();
        m_ghost->raise();
        return;
    }
    // znacznik stoi na lewej krawedzi kafla docelowego; na koncu wiersza / listy - za ostatnim kaflem
    const int n = int(m_tiles.size());
    int row = m_dragGap / m_cols, col = m_dragGap % m_cols;
    const int cursorRow = qBound(0, (pos.y() - m_y0) / m_th, (n + m_cols - 1) / m_cols - 1);
    if (m_dragGap == n || (col == 0 && row > cursorRow)) {
        row = (m_dragGap - 1) / m_cols;
        col = (m_dragGap - 1) % m_cols + 1;
    }
    const int m = ChannelTile::Margin;
    m_marker->setGeometry(m_x0 + col * m_tw - 2, m_y0 + row * m_th + m, 4, m_th - 2 * m);
    m_marker->show();
    m_ghost->raise();
    m_marker->raise();
}

void ChannelGrid::dragEnd(const QPoint& globalPos) {
    dragMove(globalPos);
    const int from = m_dragFrom, gap = m_dragGap;
    dragClear();
    if (from >= 0 && gap >= 0) m_mgr->moveChannel(from, gap);     // przebudowuje siatke
}

void ChannelGrid::dragClear() {
    delete m_ghost;
    delete m_marker;
    m_ghost = nullptr;
    m_marker = nullptr;
    m_dragFrom = m_dragGap = -1;
}
