#pragma once
#include <QDialog>
#include <QPointer>

class Channel;

// Duzy podglad jednego kanalu (dwuklik w obraz na kaflu): okno ok. 80% ekranu z obrazem 16:9,
// nazwa kanalu, stanem nagrania i miernikami audio. Na czas otwarcia kanal podaje podglad
// w wyzszej rozdzielczosci. Zamkniecie: Esc, dwuklik albo przycisk okna.
class PreviewDialog : public QDialog {
    Q_OBJECT
public:
    // Otwiera okno dla kanalu; poprzednio otwarte okno duzego podgladu jest zamykane.
    static void open(Channel* ch, int number, QWidget* parent);

    ~PreviewDialog() override;

protected:
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    PreviewDialog(Channel* ch, int number, QWidget* parent);
    void refreshTitle();

    QPointer<Channel> m_ch;
    int m_number;
};
