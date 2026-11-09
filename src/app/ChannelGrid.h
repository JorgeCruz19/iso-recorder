#pragma once
#include <QList>
#include <QScrollArea>

class ChannelManager;
class ChannelTile;
class QLabel;

// Siatka kafli N x N; gdy kanalow jest wiecej niz N*N, siatka przewija sie w pionie.
class ChannelGrid : public QScrollArea {
    Q_OBJECT
public:
    explicit ChannelGrid(ChannelManager* mgr, QWidget* parent = nullptr);
    void setColumns(int n);

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    void rebuild();
    void relayout();

    ChannelManager* m_mgr;
    QWidget* m_inner;
    QLabel* m_hint;
    QList<ChannelTile*> m_tiles;
    int m_cols = 4;
};
