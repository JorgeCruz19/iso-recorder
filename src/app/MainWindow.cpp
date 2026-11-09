#include "MainWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QVBoxLayout>

#include <windows.h>
#include <dwmapi.h>

#include "ChannelGrid.h"
#include "Inspector.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/ChannelManager.h"
#include "core/EnvCheck.h"
#include "core/Lang.h"

namespace {
QLabel* label(const QString& text, const char* objectName) {
    auto* l = new QLabel(text);
    l->setObjectName(QLatin1String(objectName));
    return l;
}

QLabel* cap(const QString& text) {
    QLabel* l = label(text.toUpper(), "cap");
    l->setMinimumWidth(QFontMetrics(Theme::font(10, QFont::Bold)).horizontalAdvance(l->text()) + 4);
    return l;
}

QFrame* cell(const char* name, QLayout* inner, int hpad) {
    auto* f = new QFrame;
    f->setObjectName(QLatin1String(name));
    inner->setContentsMargins(hpad, 0, hpad, 0);
    f->setLayout(inner);
    return f;
}

QString gb(qint64 bytes) {
    const double g = bytes / 1073741824.0;
    return g >= 1000 ? QStringLiteral("%1 TB").arg(QLocale().toString(g / 1024.0, 'f', 2))
                     : QStringLiteral("%1 GB").arg(QLocale().toString(g, 'f', 0));
}

quint64 ft(const FILETIME& f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
}

MainWindow::MainWindow(ChannelManager* mgr, QWidget* parent) : QWidget(parent), m_mgr(mgr) {
    setWindowTitle(QStringLiteral("ISO Recorder — ") + T("New session"));
    setMinimumSize(1280, 720);
    resize(1600, 900);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(HWND(winId()), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(buildHeader());
    auto* mid = new QHBoxLayout;
    mid->setSpacing(0);
    m_grid = new ChannelGrid(mgr);
    m_inspector = new Inspector(mgr);
    mid->addWidget(m_grid, 1);
    mid->addWidget(m_inspector);
    v->addLayout(mid, 1);
    v->addWidget(buildControlPanel());
    v->addWidget(buildStatus());

    connect(mgr, &ChannelManager::listChanged, this, &MainWindow::refreshCounts);
    connect(mgr, &ChannelManager::selectionChanged, this, &MainWindow::refreshCounts);
    connect(mgr, &ChannelManager::activeChanged, this, &MainWindow::refreshActive);
    connect(mgr, &ChannelManager::listChanged, this, &MainWindow::refreshActive);
    connect(mgr, &ChannelManager::notice, this, &MainWindow::notice);

    auto* second = new QTimer(this);
    connect(second, &QTimer::timeout, this, &MainWindow::tickSecond);
    second->start(1000);
    auto* frame = new QTimer(this);   // timecode RTC z klatkami (25 kl./s)
    connect(frame, &QTimer::timeout, this, [this] {
        const QTime t = QTime::currentTime();
        m_rtc->setText(t.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral(":%1").arg(t.msec() / 40, 2, 10, QLatin1Char('0')));
    });
    frame->start(40);

    refreshCounts();
    refreshActive();
    tickSecond();
}

QWidget* MainWindow::buildHeader() {
    auto* header = new QWidget;
    header->setObjectName(QStringLiteral("header"));
    header->setAttribute(Qt::WA_StyledBackground);
    header->setFixedHeight(72);
    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(0, 0, 0, 1);
    h->setSpacing(0);

    auto* vs = new QVBoxLayout;
    vs->setSpacing(3);
    vs->addStretch();
    vs->addWidget(cap(T("Session")));
    vs->addWidget(m_session = label(T("New session"), "proj"));
    vs->addStretch();
    QFrame* c = cell("cell", vs, 18);
    c->setMinimumWidth(170);
    h->addWidget(c);

    auto* vr = new QVBoxLayout;
    vr->setSpacing(0);
    vr->addStretch();
    vr->addWidget(m_recCap = cap(T("Recording time")));
    auto* hr = new QHBoxLayout;
    hr->setSpacing(16);
    hr->addWidget(m_recTime = label(QStringLiteral("00:00:00:00"), "tc"));
    hr->addWidget(m_recBadge = label(QStringLiteral("REC"), "recBadge"), 0, Qt::AlignVCenter);
    m_recBadge->hide();
    vr->addLayout(hr);
    vr->addStretch();
    c = cell("cell", vr, 18);
    c->setMinimumWidth(280);
    h->addWidget(c);

    auto* vt = new QVBoxLayout;
    vt->setSpacing(4);
    vt->addStretch();
    vt->addWidget(cap(QStringLiteral("Timecode (RTC)")));
    vt->addWidget(m_rtc = label(QStringLiteral("00:00:00:00"), "tcSmall"));
    m_rtc->setMinimumWidth(150);
    vt->addStretch();
    h->addWidget(cell("cell", vt, 18));

    auto* vd = new QVBoxLayout;
    vd->setSpacing(3);
    vd->addStretch();
    vd->addWidget(m_diskCap = cap(T("Disk")));
    auto* hd = new QHBoxLayout;
    hd->addWidget(m_diskTime = label(QStringLiteral("--:--:--"), "big"));
    hd->addStretch();
    hd->addWidget(m_diskFree = label(QString(), "dim"), 0, Qt::AlignBottom);
    vd->addLayout(hd);
    m_diskBar = new QProgressBar;
    m_diskBar->setTextVisible(false);
    m_diskBar->setRange(0, 1000);
    vd->addWidget(m_diskBar);
    vd->addStretch();
    c = cell("cell", vd, 18);
    c->setMinimumWidth(260);
    h->addWidget(c);

    auto* hs = new QHBoxLayout;
    hs->setSpacing(16);
    QLabel* dummy = nullptr;
    const struct { const char* name; const char* value; QLabel** out; } stats[] = {
        {"Write", "0 Mb/s", &dummy}, {"CPU", "—", &m_cpu}, {"GPU", "—", &dummy}, {"HW enc.", "0", &dummy}, {"Drop", "0", &dummy}};
    for (const auto& s : stats) {
        auto* vv = new QVBoxLayout;
        vv->setSpacing(2);
        vv->addStretch();
        vv->addWidget(cap(T(s.name)));
        vv->addWidget(*s.out = label(QString::fromUtf8(s.value), "stat"));
        (*s.out)->setMinimumWidth(QFontMetrics(Theme::font(15, QFont::DemiBold)).horizontalAdvance(QLatin1String(s.out == &m_cpu ? "100%" : s.name[0] == 'W' ? "999 Mb/s" : "00")) + 4);
        vv->addStretch();
        hs->addLayout(vv);
    }
    h->addWidget(cell("cell", hs, 18));

    h->addStretch(1);

    auto* hv = new QHBoxLayout;
    hv->setSpacing(0);
    auto* menuBtn = new QPushButton(QStringLiteral("☰"));
    menuBtn->setObjectName(QStringLiteral("vb"));
    menuBtn->setFocusPolicy(Qt::NoFocus);
    auto* menu = new QMenu(menuBtn);
    for (const char* t : {"New list", "Load list…", "Save list", "Save as…"})
        menu->addAction(T(t) + QStringLiteral("  ") + T("(from %1)").arg(QLatin1String(Roadmap::lists)))->setEnabled(false);
    menu->addSeparator();
    QMenu* langMenu = menu->addMenu(T("Language") + (Lang::current() == QLatin1String("en") ? QString() : QStringLiteral(" / Language")));
    for (const Lang::Info& li : Lang::available()) {
        const QString code = QLatin1String(li.code);
        QAction* a = langMenu->addAction(QString::fromUtf8(li.name), this, [this, code] { emit languageRequested(code); });
        a->setCheckable(true);
        a->setChecked(code == Lang::current());
    }
    menu->addAction(T("Diagnostics…"), this, &MainWindow::showDiagnostics);
    menu->addAction(T("About"), this, [this] {
        QMessageBox::about(this, QStringLiteral("ISO Recorder"),
                           QStringLiteral("ISO Recorder %1\n%2\n\nQt %3 · GStreamer")
                               .arg(QLatin1String(APP_VERSION), T("Multi-channel video recorder."), QLatin1String(qVersion())));
    });
    menuBtn->setMenu(menu);
    hv->addWidget(menuBtn);
    hv->addSpacing(12);
    hv->addWidget(cap(T("View")));
    hv->addSpacing(10);
    auto* group = new QButtonGroup(this);
    for (int n : {2, 3, 4}) {
        auto* b = new QPushButton(QStringLiteral("%1×%1").arg(n));
        b->setObjectName(QStringLiteral("vb"));
        b->setCheckable(true);
        b->setChecked(n == 4);
        b->setFocusPolicy(Qt::NoFocus);
        group->addButton(b, n);
        hv->addWidget(b);
    }
    auto* list = new QPushButton(T("List"));
    list->setObjectName(QStringLiteral("vb"));
    list->setEnabled(false);
    list->setToolTip(T("List view — from version %1").arg(QLatin1String(Roadmap::scheduler)));
    hv->addWidget(list);
    connect(group, &QButtonGroup::idClicked, this, [this](int n) { m_grid->setColumns(n); });
    h->addWidget(cell("viewsel", hv, 12));
    return header;
}

QWidget* MainWindow::buildControlPanel() {
    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("controlPanel"));
    panel->setAttribute(Qt::WA_StyledBackground);
    panel->setFixedHeight(86);
    auto* h = new QHBoxLayout(panel);
    h->setContentsMargins(0, 1, 0, 0);
    h->setSpacing(0);

    auto group = [&](QLabel* caption, std::initializer_list<KeyButton*> buttons) {
        auto* v = new QVBoxLayout;
        v->setSpacing(6);
        v->addStretch();
        v->addWidget(caption);
        auto* hb = new QHBoxLayout;
        hb->setSpacing(6);
        for (KeyButton* b : buttons) hb->addWidget(b);
        v->addLayout(hb);
        v->addStretch();
        h->addWidget(cell("grp", v, 16));
    };

    const struct { const char* text; KeyButton::Glyph g; } tr[4] = {
        {"REC", KeyButton::Dot}, {"PAUSE", KeyButton::PauseBars}, {"STOP", KeyButton::Square}, {"SPLIT", KeyButton::Cut}};
    for (int i = 0; i < 4; ++i) {
        m_btnTransport[i] = new KeyButton(QLatin1String(tr[i].text), tr[i].g, false);
        connect(m_btnTransport[i], &KeyButton::clicked, this, [this, i] { m_mgr->transportSelected(Transport(i)); });
    }
    m_btnTransport[0]->setRed(true);
    group(m_selCap = cap(T("Selected channels (%1)").arg(0)),
          {m_btnTransport[0], m_btnTransport[1], m_btnTransport[2], m_btnTransport[3]});

    // Zaznaczenie: dotyczy pol wyboru na kaflach
    m_btnSelect[0] = new KeyButton(T("SELECT ALL"), KeyButton::None, true);
    m_btnSelect[1] = new KeyButton(T("SELECT NONE"), KeyButton::None, true);
    m_btnSelect[2] = new KeyButton(T("INVERT"), KeyButton::None, true);
    connect(m_btnSelect[0], &KeyButton::clicked, m_mgr, &ChannelManager::selectAll);
    connect(m_btnSelect[1], &KeyButton::clicked, m_mgr, &ChannelManager::selectNone);
    connect(m_btnSelect[2], &KeyButton::clicked, m_mgr, &ChannelManager::invertSelection);
    group(cap(T("Selection")), {m_btnSelect[0], m_btnSelect[1], m_btnSelect[2]});

    // Kanal aktywny (ten z biala ramka, pokazany w inspektorze)
    m_btnUp = new KeyButton(T("MOVE UP"), KeyButton::Up, false);
    m_btnDown = new KeyButton(T("MOVE DOWN"), KeyButton::Down, false);
    connect(m_btnUp, &KeyButton::clicked, this, [this] { m_mgr->moveActive(-1); });
    connect(m_btnDown, &KeyButton::clicked, this, [this] { m_mgr->moveActive(1); });
    group(cap(T("Active channel")), {m_btnUp, m_btnDown});

    // Lista kanalow jako calosc
    m_btnAdd = new KeyButton(T("ADD SOURCE"), KeyButton::None, true);
    auto* load = new KeyButton(T("LOAD"), KeyButton::None, true);
    auto* save = new KeyButton(T("SAVE"), KeyButton::None, true);
    load->setEnabled(false);
    save->setEnabled(false);
    load->setToolTip(T("From version %1").arg(QLatin1String(Roadmap::lists)));
    save->setToolTip(T("From version %1").arg(QLatin1String(Roadmap::lists)));
    connect(m_btnAdd, &KeyButton::clicked, m_mgr, [this] { m_mgr->addChannel(); });
    group(cap(T("Channel list")), {m_btnAdd, load, save});

    h->addStretch(1);

    auto* vc = new QVBoxLayout;
    vc->setSpacing(2);
    vc->addStretch();
    vc->addWidget(cap(T("Channels")), 0, Qt::AlignRight);
    vc->addWidget(m_count = label(QString(), "countN"), 0, Qt::AlignRight);
    m_count->setTextFormat(Qt::RichText);
    vc->addWidget(m_legend = label(QString(), "legend"), 0, Qt::AlignRight);
    m_legend->setTextFormat(Qt::RichText);
    vc->addStretch();
    h->addWidget(cell("count", vc, 20));
    return panel;
}

QWidget* MainWindow::buildStatus() {
    auto* st = new QWidget;
    st->setObjectName(QStringLiteral("status"));
    st->setAttribute(Qt::WA_StyledBackground);
    st->setFixedHeight(22);
    auto* h = new QHBoxLayout(st);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(22);
    auto* autosave = new QLabel(QStringLiteral("<b style='color:#9d9d9d'>Autosave</b> — ") + T("(from version %1)").arg(QLatin1String(Roadmap::lists)));
    autosave->setTextFormat(Qt::RichText);
    h->addWidget(autosave);
    h->addWidget(m_events = new QLabel, 1);
    h->addWidget(m_hint = new QLabel);
    return st;
}

void MainWindow::notice(const QString& text) {
    m_log.prepend(QTime::currentTime().toString(QStringLiteral("HH:mm:ss ")) + text);
    while (m_log.size() > 2) m_log.removeLast();
    m_events->setText(m_log.join(QStringLiteral("      ")));
}

void MainWindow::refreshCounts() {
    const int n = m_mgr->count(), sel = m_mgr->selectedCount();
    m_selCap->setText(T("Selected channels (%1)").arg(sel).toUpper());
    for (KeyButton* b : m_btnTransport) b->setEnabled(sel > 0);
    m_btnSelect[0]->setEnabled(n > 0 && sel < n);
    m_btnSelect[1]->setEnabled(sel > 0);
    m_btnSelect[2]->setEnabled(n > 0);
    m_btnAdd->setEnabled(m_mgr->canAdd());
    m_count->setText(QStringLiteral("%1<span style='color:#6e6e6e'> / %2</span>").arg(n).arg(ChannelManager::MaxChannels));

    int rec = 0, pau = 0, prv = 0, wait = 0, err = 0;
    for (Channel* c : m_mgr->channels()) {
        switch (c->state()) {
        case ChannelState::Recording: case ChannelState::Reconnecting: ++rec; break;
        case ChannelState::Paused: ++pau; break;
        case ChannelState::Preview: ++prv; break;
        case ChannelState::Waiting: ++wait; break;
        case ChannelState::Error: ++err; break;
        default: break;
        }
    }
    auto item = [](const char* color, const QString& text) {
        return QStringLiteral("<span style='color:%1'>■</span> %2").arg(QLatin1String(color), text);
    };
    QStringList lg{item("#e0242b", QStringLiteral("REC %1").arg(rec)), item("#f2a516", T("PAUSED %1").arg(pau)),
                   item("#1fae52", QStringLiteral("PREVIEW %1").arg(prv)), item("#555555", T("WAITING %1").arg(wait))};
    if (err) lg << item("#e0242b", T("ERROR %1").arg(err));
    m_legend->setText(lg.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")));
    m_btnTransport[0]->setLit(rec > 0);
    m_hint->setText(m_mgr->canAdd() ? QString() : T("Limit of 16 channels reached — adding sources is disabled"));
}

void MainWindow::refreshActive() {
    Channel* ch = m_mgr->active();
    const int i = m_mgr->indexOf(ch);
    m_recCap->setText((ch ? T("Recording time — channel %1").arg(i + 1, 2, 10, QLatin1Char('0')) : T("Recording time")).toUpper());
    m_btnUp->setEnabled(i > 0);
    m_btnDown->setEnabled(i >= 0 && i < m_mgr->count() - 1);
}

void MainWindow::tickSecond() {
    // dysk domyslnego folderu nagran
    const QStorageInfo disk(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    if (disk.isValid() && disk.bytesTotal() > 0) {
        m_diskCap->setText(T("Disk").toUpper() + QLatin1Char(' ') + disk.rootPath().left(2));
        m_diskFree->setText(T("%1 free of %2").arg(gb(disk.bytesAvailable()), gb(disk.bytesTotal())));
        m_diskBar->setValue(int(1000.0 * (disk.bytesTotal() - disk.bytesAvailable()) / disk.bytesTotal()));
    }
    // CPU calego systemu
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const quint64 i = ft(idle), t = ft(kernel) + ft(user);
        if (m_cpuTotal && t > m_cpuTotal)
            m_cpu->setText(QStringLiteral("%1%").arg(qRound(100.0 * (1.0 - double(i - m_cpuIdle) / double(t - m_cpuTotal)))));
        m_cpuIdle = i;
        m_cpuTotal = t;
    }
}

void MainWindow::showDiagnostics() {
    QDialog dlg(this);
    dlg.setWindowTitle(T("Environment diagnostics"));
    dlg.resize(520, 640);
    auto* v = new QVBoxLayout(&dlg);
    auto* text = new QPlainTextEdit(EnvCheck::run().report);
    text->setReadOnly(true);
    v->addWidget(text);
    dlg.exec();
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    m_inspector->setFixedWidth(width() >= 1600 ? 500 : 400);
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    Channel* ch = m_mgr->active();
    const int i = m_mgr->indexOf(ch), n = m_mgr->count();
    switch (e->key()) {
    case Qt::Key_A: if (ctrl && shift) m_mgr->selectNone(); else if (ctrl) m_mgr->selectAll(); break;
    case Qt::Key_I: if (ctrl) m_mgr->invertSelection(); break;
    case Qt::Key_Delete: if (ch) m_mgr->removeChannel(ch); break;
    case Qt::Key_Up: if (ctrl) m_mgr->moveActive(-1); break;
    case Qt::Key_Down: if (ctrl) m_mgr->moveActive(1); break;
    case Qt::Key_Left: if (i > 0) m_mgr->setActive(m_mgr->channels().at(i - 1)); break;
    case Qt::Key_Right: if (i >= 0 && i < n - 1) m_mgr->setActive(m_mgr->channels().at(i + 1)); break;
    case Qt::Key_Space: if (ch) ch->setSelected(!ch->isSelected()); break;
    default: QWidget::keyPressEvent(e);
    }
}
