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
#include "Widgets.h"
#include "core/ChannelManager.h"
#include "core/EnvCheck.h"

namespace {
QLabel* label(const QString& text, const char* objectName) {
    auto* l = new QLabel(text);
    l->setObjectName(QLatin1String(objectName));
    return l;
}

QLabel* cap(const QString& text) { return label(text.toUpper(), "cap"); }

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
    setWindowTitle(QStringLiteral("ISO Recorder — Nowa sesja"));
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
    vs->addWidget(cap(QStringLiteral("Sesja")));
    vs->addWidget(m_session = label(QStringLiteral("Nowa sesja"), "proj"));
    vs->addStretch();
    QFrame* c = cell("cell", vs, 22);
    c->setMinimumWidth(200);
    h->addWidget(c);

    auto* vr = new QVBoxLayout;
    vr->setSpacing(0);
    vr->addStretch();
    vr->addWidget(m_recCap = cap(QStringLiteral("Czas nagrania")));
    auto* hr = new QHBoxLayout;
    hr->setSpacing(16);
    hr->addWidget(m_recTime = label(QStringLiteral("00:00:00:00"), "tc"));
    hr->addWidget(m_recBadge = label(QStringLiteral("REC"), "recBadge"), 0, Qt::AlignVCenter);
    m_recBadge->hide();
    vr->addLayout(hr);
    vr->addStretch();
    c = cell("cell", vr, 22);
    c->setMinimumWidth(300);
    h->addWidget(c);

    auto* vt = new QVBoxLayout;
    vt->setSpacing(4);
    vt->addStretch();
    vt->addWidget(cap(QStringLiteral("Timecode (RTC)")));
    vt->addWidget(m_rtc = label(QStringLiteral("00:00:00:00"), "tcSmall"));
    m_rtc->setMinimumWidth(150);
    vt->addStretch();
    h->addWidget(cell("cell", vt, 22));

    auto* vd = new QVBoxLayout;
    vd->setSpacing(3);
    vd->addStretch();
    vd->addWidget(m_diskCap = cap(QStringLiteral("Dysk")));
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
    c = cell("cell", vd, 22);
    c->setMinimumWidth(300);
    h->addWidget(c);

    auto* hs = new QHBoxLayout;
    hs->setSpacing(22);
    QLabel* dummy = nullptr;
    const struct { const char* name; const char* value; QLabel** out; } stats[] = {
        {"Zapis", "0 Mb/s", &dummy}, {"CPU", "—", &m_cpu}, {"GPU", "—", &dummy}, {"Enk. HW", "0", &dummy}, {"Drop", "0", &dummy}};
    for (const auto& s : stats) {
        auto* vv = new QVBoxLayout;
        vv->setSpacing(2);
        vv->addStretch();
        vv->addWidget(cap(QString::fromUtf8(s.name)));
        vv->addWidget(*s.out = label(QString::fromUtf8(s.value), "stat"));
        vv->addStretch();
        hs->addLayout(vv);
    }
    h->addWidget(cell("cell", hs, 22));

    h->addStretch(1);

    auto* hv = new QHBoxLayout;
    hv->setSpacing(0);
    auto* menuBtn = new QPushButton(QStringLiteral("☰"));
    menuBtn->setObjectName(QStringLiteral("vb"));
    menuBtn->setFocusPolicy(Qt::NoFocus);
    auto* menu = new QMenu(menuBtn);
    for (const char* t : {"Nowa lista", "Wczytaj listę…", "Zapisz listę", "Zapisz jako…"})
        menu->addAction(QString::fromUtf8(t) + QStringLiteral("  (od 0.5)"))->setEnabled(false);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Diagnostyka…"), this, &MainWindow::showDiagnostics);
    menu->addAction(QStringLiteral("O programie"), this, [this] {
        QMessageBox::about(this, QStringLiteral("ISO Recorder"),
                           QStringLiteral("ISO Recorder %1\nWielokanałowy rejestrator wideo.\n\nQt %2 · GStreamer")
                               .arg(QLatin1String(APP_VERSION), QLatin1String(qVersion())));
    });
    menuBtn->setMenu(menu);
    hv->addWidget(menuBtn);
    hv->addSpacing(18);
    hv->addWidget(cap(QStringLiteral("Widok")));
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
    auto* list = new QPushButton(QStringLiteral("Lista"));
    list->setObjectName(QStringLiteral("vb"));
    list->setEnabled(false);
    list->setToolTip(QStringLiteral("Widok listy — od wersji 0.9"));
    hv->addWidget(list);
    connect(group, &QButtonGroup::idClicked, this, [this](int n) { m_grid->setColumns(n); });
    h->addWidget(cell("viewsel", hv, 18));
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
    group(m_selCap = cap(QStringLiteral("Zaznaczone kanały (0)")),
          {m_btnTransport[0], m_btnTransport[1], m_btnTransport[2], m_btnTransport[3]});

    m_btnSelectAll = new KeyButton(QStringLiteral("SELECT ALL"), KeyButton::None, true);
    m_btnAdd = new KeyButton(QStringLiteral("ADD SOURCE"), KeyButton::None, true);
    connect(m_btnSelectAll, &KeyButton::clicked, m_mgr, &ChannelManager::selectAllToggle);
    connect(m_btnAdd, &KeyButton::clicked, m_mgr, [this] { m_mgr->addChannel(); });
    group(cap(QStringLiteral("Wybór")), {m_btnSelectAll, m_btnAdd});

    m_btnUp = new KeyButton(QStringLiteral("W GÓRĘ"), KeyButton::Up, false);
    m_btnDown = new KeyButton(QStringLiteral("W DÓŁ"), KeyButton::Down, false);
    auto* load = new KeyButton(QStringLiteral("WCZYTAJ"), KeyButton::None, true);
    auto* save = new KeyButton(QStringLiteral("ZAPISZ"), KeyButton::None, true);
    load->setEnabled(false);
    save->setEnabled(false);
    load->setToolTip(QStringLiteral("Od wersji 0.5"));
    save->setToolTip(QStringLiteral("Od wersji 0.5"));
    connect(m_btnUp, &KeyButton::clicked, this, [this] { m_mgr->moveActive(-1); });
    connect(m_btnDown, &KeyButton::clicked, this, [this] { m_mgr->moveActive(1); });
    group(cap(QStringLiteral("Lista kanałów")), {m_btnUp, m_btnDown, load, save});

    h->addStretch(1);

    auto* vc = new QVBoxLayout;
    vc->setSpacing(2);
    vc->addStretch();
    vc->addWidget(cap(QStringLiteral("Kanały")), 0, Qt::AlignRight);
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
    auto* autosave = new QLabel(QStringLiteral("<b style='color:#9d9d9d'>Autosave</b> — (od wersji 0.5)"));
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
    m_selCap->setText(QStringLiteral("ZAZNACZONE KANAŁY (%1)").arg(sel));
    for (KeyButton* b : m_btnTransport) b->setEnabled(sel > 0);
    m_btnSelectAll->setEnabled(n > 0);
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
    QStringList lg{item("#e0242b", QStringLiteral("REC %1").arg(rec)), item("#f2a516", QStringLiteral("PAUZA %1").arg(pau)),
                   item("#1fae52", QStringLiteral("PREVIEW %1").arg(prv)), item("#555555", QStringLiteral("CZEKA %1").arg(wait))};
    if (err) lg << item("#e0242b", QStringLiteral("BŁĄD %1").arg(err));
    m_legend->setText(lg.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")));
    m_btnTransport[0]->setLit(rec > 0);
    m_hint->setText(m_mgr->canAdd() ? QString() : QStringLiteral("Limit 16 kanałów osiągnięty — ADD SOURCE nieaktywny"));
}

void MainWindow::refreshActive() {
    Channel* ch = m_mgr->active();
    const int i = m_mgr->indexOf(ch);
    m_recCap->setText(ch ? QStringLiteral("CZAS NAGRANIA — KANAŁ %1").arg(i + 1, 2, 10, QLatin1Char('0'))
                         : QStringLiteral("CZAS NAGRANIA"));
    m_btnUp->setEnabled(i > 0);
    m_btnDown->setEnabled(i >= 0 && i < m_mgr->count() - 1);
}

void MainWindow::tickSecond() {
    // dysk domyslnego folderu nagran
    const QStorageInfo disk(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    if (disk.isValid() && disk.bytesTotal() > 0) {
        m_diskCap->setText(QStringLiteral("DYSK %1").arg(disk.rootPath().left(2)));
        m_diskFree->setText(QStringLiteral("%1 wolne z %2").arg(gb(disk.bytesAvailable()), gb(disk.bytesTotal())));
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
    dlg.setWindowTitle(QStringLiteral("Diagnostyka środowiska"));
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
    Channel* ch = m_mgr->active();
    const int i = m_mgr->indexOf(ch), n = m_mgr->count();
    switch (e->key()) {
    case Qt::Key_A: if (ctrl) m_mgr->selectAllToggle(); break;
    case Qt::Key_Delete: if (ch) m_mgr->removeChannel(ch); break;
    case Qt::Key_Up: if (ctrl) m_mgr->moveActive(-1); break;
    case Qt::Key_Down: if (ctrl) m_mgr->moveActive(1); break;
    case Qt::Key_Left: if (i > 0) m_mgr->setActive(m_mgr->channels().at(i - 1)); break;
    case Qt::Key_Right: if (i >= 0 && i < n - 1) m_mgr->setActive(m_mgr->channels().at(i + 1)); break;
    case Qt::Key_Space: if (ch) ch->setSelected(!ch->isSelected()); break;
    default: QWidget::keyPressEvent(e);
    }
}
