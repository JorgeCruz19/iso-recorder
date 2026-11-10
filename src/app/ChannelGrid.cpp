#include "ChannelGrid.h"

#include <QLabel>

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
    for (ChannelTile* t : m_tiles) {
        t->hide();
        t->deleteLater();
    }
    m_tiles.clear();
    int n = 0;
    for (Channel* ch : m_mgr->channels()) {
        auto* t = new ChannelTile(m_mgr, ch, ++n, m_inner);
        t->show();
        m_tiles.append(t);
    }
    m_hint->setVisible(m_tiles.isEmpty());
    relayout();
}

void ChannelGrid::relayout() {
    // Kadr kafla ma zawsze 16:9, wiec rozmiar kafla wynika z ciasniejszego z dwoch wymiarow;
    // siatka jest wysrodkowana w poziomie, a wolne miejsce zostaje tlem.
    const int pad = 2;   // kafle maja wlasny margines na ramke kanalu aktywnego
    const QSize vp = viewport()->size();
    const int availW = (vp.width() - 2 * pad) / m_cols;
    const int availH = (vp.height() - 2 * pad) / m_cols;
    const int tw = qMax(120, qMin(availW, ChannelTile::tileWidthForHeight(availH)));
    const int th = ChannelTile::heightForTileWidth(tw);
    const int rows = (int(m_tiles.size()) + m_cols - 1) / m_cols;
    m_inner->resize(vp.width(), qMax(vp.height(), 2 * pad + rows * th));
    m_hint->setGeometry(m_inner->rect());
    const int x0 = (vp.width() - m_cols * tw) / 2;
    for (int i = 0; i < m_tiles.size(); ++i)
        m_tiles[i]->setGeometry(x0 + (i % m_cols) * tw, pad + (i / m_cols) * th, tw, th);
}
