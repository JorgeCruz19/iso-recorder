#pragma once
#include <QAbstractButton>
#include <QPointer>
#include <QWidget>

class Channel;

// Duzy przycisk panelu sterowania (.kb z mockupu) z rysowanym symbolem.
class KeyButton : public QAbstractButton {
    Q_OBJECT
public:
    enum Glyph { None, Dot, PauseBars, Square, Cut, Up, Down };
    KeyButton(const QString& text, Glyph glyph, bool wide, QWidget* parent = nullptr);
    void setRed(bool on) { m_red = on; update(); }
    void setLit(bool on) { if (m_lit != on) { m_lit = on; update(); } }
    QSize sizeHint() const override { return {m_width, 42}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Glyph m_glyph;
    bool m_wide;
    int m_width = 72;
    bool m_red = false;
    bool m_lit = false;
};

// Podglad aktywnego kanalu w inspektorze - ta sama klatka co na kaflu.
class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(QWidget* parent = nullptr);
    void setChannel(Channel* ch, int number);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QPointer<Channel> m_ch;
    int m_number = 0;
};

// Poziome mierniki L/R ze skala dBFS.
class MeterPane : public QWidget {
    Q_OBJECT
public:
    explicit MeterPane(QWidget* parent = nullptr);
    void setChannel(Channel* ch);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QPointer<Channel> m_ch;
};
