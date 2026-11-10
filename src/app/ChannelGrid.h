#pragma once
#include <QList>
#include <QScrollArea>

class ChannelManager;
class ChannelTile;
class QLabel;
class QPoint;

// Siatka kafli N x N; gdy kanalow jest wiecej niz N*N, siatka przewija sie w pionie.
class ChannelGrid : public QScrollArea {
    Q_OBJECT
public:
    explicit ChannelGrid(ChannelManager* mgr, QWidget* parent = nullptr);
    void setColumns(int n);
    // Szerokosc bloku siatki, przy ktorej cols x cols kafli 16:9 wypelnia wysokosc h z rownymi marginesami.
    static int widthForHeight(int h, int cols);
    static constexpr int Pad = 4;   // margines zewnetrzny = polowa odstepu miedzy kadrami (kafel ma wlasne 4 px)

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    void rebuild();
    void relayout();
    // Zmiana kolejnosci przeciaganiem: polprzezroczysta kopia kafla idzie za kursorem,
    // pomaranczowa kreska pokazuje miejsce wstawienia. Upuszczenie poza siatka anuluje.
    void dragBegin(ChannelTile* tile);
    void dragMove(const QPoint& globalPos);
    void dragEnd(const QPoint& globalPos);
    void dragClear();
    int gapAt(const QPoint& innerPos) const;

    ChannelManager* m_mgr;
    QWidget* m_inner;
    QLabel* m_hint;
    QList<ChannelTile*> m_tiles;
    int m_cols = 4;
    int m_x0 = 0, m_y0 = 0, m_tw = 1, m_th = 1;     // biezacy uklad kafli
    int m_dragFrom = -1;                            // indeks przeciaganego kafla (-1 = brak)
    int m_dragGap = -1;
    QLabel* m_ghost = nullptr;
    QWidget* m_marker = nullptr;
};
