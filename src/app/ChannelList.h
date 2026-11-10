#pragma once
#include <QList>
#include <QScrollArea>

class Channel;
class ChannelManager;
class ChannelRow;
class QLabel;
enum class Transport;

// Widok listy: jeden wiersz na kanal - miniatura, nazwa, zrodlo, format, stan, czas, strumien zapisu, wyjscie,
// mierniki i przyciski transportu. To samo zachowanie co kafle (klik = kanal aktywny, pole = zaznaczenie,
// dwuklik w miniature = duzy podglad, menu kontekstowe), tylko gestsze - wygodne przy wielu kanalach.
class ChannelList : public QScrollArea {
    Q_OBJECT
public:
    explicit ChannelList(ChannelManager* mgr, QWidget* parent = nullptr);

signals:
    // przyciski w naglowku: REC / II / STOP / SPLIT dla zaznaczonych kanalow
    void transportSelectedRequested(Transport t);

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    void rebuild();
    void relayout();

    ChannelManager* m_mgr;
    QWidget* m_inner;
    QWidget* m_header;
    QLabel* m_hint;
    QList<ChannelRow*> m_rows;
};

// Polozenie kolumn wiersza dla danej szerokosci - wspolne dla naglowka i wierszy.
struct ListColumns {
    int sel, num, thumb, name, source, format, state, time, rate, live, meter, buttons, close, end;
    static ListColumns forWidth(int w, int thumbW);
};

class ChannelRow : public QWidget {
    Q_OBJECT
public:
    ChannelRow(ChannelManager* mgr, Channel* ch, int number, QWidget* parent = nullptr);
    // Wysokosc wiersza wynika z miejsca: 16 kanalow (maksimum) miesci sie bez przewijania, a przy mniejszej liczbie
    // wiersze i miniatury rosna - do MaxRowH.
    static constexpr int MinRowH = 40, MaxRowH = 84;
    static int thumbWidthFor(int rowH) { return (rowH - 8) * 16 / 9; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    QRect buttonRect(const ListColumns& c, int i) const;
    ChannelManager* m_mgr;
    Channel* m_ch;
    int m_number;
};
