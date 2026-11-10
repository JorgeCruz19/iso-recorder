#include "PreviewDialog.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

#include "ChannelTile.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Channel.h"
#include "core/ChannelManager.h"
#include "core/Lang.h"

namespace {

// Obraz 16:9 wpisany w dostepne miejsce, z nakladkami jak na kaflu (format, czas nagrania, nazwa).
class BigView : public QWidget {
public:
    BigView(Channel* ch, int number, QWidget* parent) : QWidget(parent), m_ch(ch), m_number(number) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        connect(ch, &Channel::frameChanged, this, qOverload<>(&QWidget::update));
        connect(ch, &Channel::changed, this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x0b, 0x0b, 0x0b));
        if (!m_ch) return;
        // kadr 16:9, wysrodkowany
        int w = width(), h = w * 9 / 16;
        if (h > height()) { h = height(); w = h * 16 / 9; }
        const QRect vid((width() - w) / 2, (height() - h) / 2, w, h);
        p.fillRect(vid, Qt::black);
        const QImage& img = m_ch->frame();
        if (!img.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform, vid.width() != img.width());
            p.drawImage(vid, img);
        }
        const ChannelState st = m_ch->state();
        if (st == ChannelState::Error || st == ChannelState::Idle || st == ChannelState::Reconnecting) {
            p.fillRect(vid, QColor(0, 0, 0, 158));
            p.setPen(Qt::white);
            p.setFont(Theme::font(22, QFont::Bold, 1));
            p.drawText(vid, Qt::AlignCenter, st == ChannelState::Error ? T("ERROR")
                                           : st == ChannelState::Idle ? T("NO SIGNAL") : T("RECONNECTING"));
        }
        // ramka stanu - te same kolory co na kaflu
        QColor frame = Theme::idle;
        if (st == ChannelState::Recording || st == ChannelState::Reconnecting || st == ChannelState::Error ||
            st == ChannelState::Finalizing) frame = Theme::red;
        else if (st == ChannelState::Paused) frame = Theme::amber;
        else if (st == ChannelState::Preview) frame = Theme::green;
        p.setPen(QPen(frame, 4, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(vid).adjusted(2, 2, -2, -2));

        const QColor chip(0, 0, 0, 178);
        // format (lewy gorny rog)
        p.setFont(Theme::font(13, QFont::Bold, 0.3));
        const QString fmt = m_ch->formatText();
        const QRect fr(vid.left() + 16, vid.top() + 16, p.fontMetrics().horizontalAdvance(fmt) + 18, 24);
        p.fillRect(fr, chip);
        p.setPen(Qt::white);
        p.drawText(fr, Qt::AlignCenter, fmt);
        // czas nagrania (prawy gorny rog)
        if (m_ch->isRecording()) {
            const qint64 sec = m_ch->recordMs() / 1000;
            QString t = st == ChannelState::Finalizing ? T("SAVING")
                : QStringLiteral("%1:%2:%3").arg(sec / 3600, 2, 10, QLatin1Char('0')).arg(sec / 60 % 60, 2, 10, QLatin1Char('0'))
                                             .arg(sec % 60, 2, 10, QLatin1Char('0'));
            if (st != ChannelState::Finalizing && m_ch->recordPart() > 1)
                t += QStringLiteral("  ·  %1").arg(m_ch->recordPart());
            p.setFont(Theme::font(16, QFont::Bold));
            const int tw = p.fontMetrics().horizontalAdvance(t);
            const QRect rr(vid.right() - 16 - (tw + 40), vid.top() + 16, tw + 40, 28);
            p.fillRect(rr, chip);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::red);
            p.drawEllipse(QRectF(rr.left() + 10, rr.top() + 8, 12, 12));
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(Qt::white);
            p.drawText(rr.adjusted(30, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, t);
        }
        // nazwa kanalu (dol, srodek)
        const QString num = QStringLiteral("%1").arg(m_number, 2, 10, QLatin1Char('0'));
        const QFont f = Theme::font(20, QFont::Bold);
        p.setFont(f);
        const int wn = p.fontMetrics().horizontalAdvance(num);
        const int maxName = vid.width() - 80 - wn;
        const QString name = p.fontMetrics().elidedText(m_ch->config().name, Qt::ElideRight, maxName);
        const int lw = wn + 12 + p.fontMetrics().horizontalAdvance(name) + 32;
        const QRect lr(vid.left() + (vid.width() - lw) / 2, vid.bottom() - 18 - 36, lw, 36);
        p.fillRect(lr, QColor(0, 0, 0, 209));
        p.setPen(QColor(0xbd, 0xbd, 0xbd));
        p.drawText(lr.adjusted(16, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, num);
        p.setPen(Qt::white);
        p.drawText(lr.adjusted(16 + wn + 12, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, name);
    }

private:
    QPointer<Channel> m_ch;
    int m_number;
};

QPointer<PreviewDialog> g_open;

}  // namespace

void PreviewDialog::open(ChannelManager* mgr, Channel* ch, int number, QWidget* parent) {
    if (g_open) g_open->close();
    if (!ch) return;
    g_open = new PreviewDialog(mgr, ch, number, parent);
    g_open->show();
    g_open->raise();
    g_open->activateWindow();
}

PreviewDialog::PreviewDialog(ChannelManager* mgr, Channel* ch, int number, QWidget* parent)
    : QDialog(parent), m_ch(ch), m_number(number) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setStyleSheet(QStringLiteral("QDialog { background:#0b0b0b; }"));

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(new BigView(ch, number, this), 1);
    // pasek pod obrazem: przyciski transportu tego kanalu, obok mierniki audio
    auto* bar = new QWidget(this);
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setObjectName(QStringLiteral("bigBar"));
    bar->setStyleSheet(QStringLiteral("#bigBar { background:#2a2a2a; }"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 8, 0, 8);
    h->setSpacing(6);
    const struct { const char* text; KeyButton::Glyph g; } tr[4] = {
        {"REC", KeyButton::Dot}, {"PAUSE", KeyButton::PauseBars}, {"STOP", KeyButton::Square}, {"SPLIT", KeyButton::Cut}};
    for (int i = 0; i < 4; ++i) {
        m_btn[i] = new KeyButton(QLatin1String(tr[i].text), tr[i].g, false, bar);
        m_btn[i]->setFixedWidth(104);
        connect(m_btn[i], &QAbstractButton::clicked, this, [this, mgr, i] {
            if (m_ch) mgr->transport(m_ch, Transport(i));
        });
        h->addWidget(m_btn[i]);
    }
    m_btn[0]->setRed(true);
    auto* meters = new MeterPane(bar);
    meters->setChannel(ch);
    h->addWidget(meters, 1);
    v->addWidget(bar);

    // ok. 80% ekranu, na ktorym jest okno programu
    QScreen* scr = parent && parent->screen() ? parent->screen() : QGuiApplication::primaryScreen();
    const QRect area = scr->availableGeometry();
    resize(area.width() * 8 / 10, area.height() * 8 / 10);
    move(area.center() - QPoint(width() / 2, height() / 2));

    connect(ch, &Channel::changed, this, &PreviewDialog::refresh);
    connect(ch, &QObject::destroyed, this, &QWidget::close);      // kanal usuniety - okno znika
    refresh();
    ch->setLargePreview(true);
}

PreviewDialog::~PreviewDialog() {
    if (m_ch) m_ch->setLargePreview(false);
}

void PreviewDialog::refresh() {
    if (!m_ch) return;
    for (int i = 0; i < 4; ++i) {
        m_btn[i]->setEnabled(ChannelTile::transportEnabled(m_ch->state(), i));
        m_btn[i]->setLit(ChannelTile::transportLit(m_ch->state(), i));
    }
    setWindowTitle(QStringLiteral("%1  %2 — ISO Recorder").arg(m_number, 2, 10, QLatin1Char('0')).arg(m_ch->config().name));
}

void PreviewDialog::mouseDoubleClickEvent(QMouseEvent* e) {
    // dwuklik w pasek z przyciskami i miernikami nie zamyka okna
    if (const QWidget* bar = findChild<QWidget*>(QStringLiteral("bigBar")); bar && bar->geometry().contains(e->position().toPoint())) return;
    close();
}
