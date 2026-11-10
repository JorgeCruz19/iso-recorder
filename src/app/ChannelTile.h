#pragma once
#include <QWidget>

class Channel;
class ChannelManager;

// Kafel kanalu - jeden widget malowany w calosci QPainterem (bez widgetow potomnych).
class ChannelTile : public QWidget {
    Q_OBJECT
public:
    ChannelTile(ChannelManager* mgr, Channel* ch, int number, QWidget* parent = nullptr);

    static constexpr int Margin = 4;     // miejsce na ramke kanalu aktywnego
    static constexpr int StripH = 26;
    static int heightForTileWidth(int w);
    static int tileWidthForHeight(int h);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    struct Rects {
        QRect vid, strip, selBox, btn[4], grip, close, label;
        int srcLeft = 0;
    };
    Rects rects() const;
    bool buttonEnabled(int i) const;
    bool buttonLit(int i) const;

    ChannelManager* m_mgr;
    Channel* m_ch;
    int m_number;
};
