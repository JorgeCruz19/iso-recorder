#include "MainWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QCloseEvent>
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
#include "ChannelTile.h"
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
    // Prawa kolumna: inspektor, pod nim panel sterowania. Siatka wideo zajmuje cala wysokosc po lewej.
    m_right = new QWidget;
    auto* rv = new QVBoxLayout(m_right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);
    rv->addWidget(m_inspector, 1);
    rv->addWidget(buildControlPanel());
    mid->addWidget(m_grid, 1);
    mid->addWidget(m_right);
    v->addLayout(mid, 1);
    v->addWidget(buildStatus());

    connect(mgr, &ChannelManager::listChanged, this, &MainWindow::refreshCounts);
    connect(mgr, &ChannelManager::selectionChanged, this, &MainWindow::refreshCounts);
    connect(mgr, &ChannelManager::activeChanged, this, &MainWindow::refreshActive);
    connect(mgr, &ChannelManager::listChanged, this, &MainWindow::refreshActive);
    connect(mgr, &ChannelManager::notice, this, &MainWindow::notice);
    connect(mgr, &ChannelManager::statsChanged, this, &MainWindow::refreshRecording);
    connect(mgr, &ChannelManager::selectionChanged, this, &MainWindow::refreshRecording);
    connect(mgr, &ChannelManager::activeChanged, this, &MainWindow::refreshRecording);

    auto* second = new QTimer(this);
    connect(second, &QTimer::timeout, this, &MainWindow::tickSecond);
    second->start(1000);
    auto* frame = new QTimer(this);   // timecode RTC z klatkami (25 kl./s)
    connect(frame, &QTimer::timeout, this, [this] {
        const QTime t = QTime::currentTime();
        m_rtc->setText(t.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral(":%1").arg(t.msec() / 40, 2, 10, QLatin1Char('0')));
        // czas nagrania kanalu aktywnego, z klatkami
        const Channel* ch = m_mgr->active();
        const qint64 ms = ch ? ch->recordMs() : 0;
        m_recTime->setText(QStringLiteral("%1:%2:%3:%4").arg(ms / 3600000, 2, 10, QLatin1Char('0')).arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
                               .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000 / 40, 2, 10, QLatin1Char('0')));
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
    header->setFixedHeight(HeaderH);
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
        {"Write", "0 Mb/s", &m_write}, {"CPU", "—", &m_cpu}, {"GPU", "—", &dummy}, {"HW enc.", "0", &m_hwEnc}, {"Drop", "0", &dummy}};
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

// Panel sterowania stoi w prawej kolumnie pod inspektorem: cztery grupy jedna pod druga.
QWidget* MainWindow::buildControlPanel() {
    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("controlPanel"));
    panel->setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(1, 1, 0, 0);
    v->setSpacing(0);

    auto group = [&](QLabel* caption, std::initializer_list<KeyButton*> buttons, QWidget* right = nullptr) {
        auto* f = new QFrame;
        f->setObjectName(QStringLiteral("grp"));
        auto* gv = new QVBoxLayout(f);
        gv->setContentsMargins(12, 7, 12, 9);
        gv->setSpacing(5);
        auto* top = new QHBoxLayout;
        top->addWidget(caption);
        top->addStretch(1);
        if (right) top->addWidget(right);
        gv->addLayout(top);
        auto* hb = new QHBoxLayout;
        hb->setSpacing(6);
        for (KeyButton* b : buttons) hb->addWidget(b, 1);
        gv->addLayout(hb);
        v->addWidget(f);
    };

    const struct { const char* text; KeyButton::Glyph g; } tr[4] = {
        {"REC", KeyButton::Dot}, {"PAUSE", KeyButton::PauseBars}, {"STOP", KeyButton::Square}, {"SPLIT", KeyButton::Cut}};
    for (int i = 0; i < 4; ++i) {
        m_btnTransport[i] = new KeyButton(QLatin1String(tr[i].text), tr[i].g, false);
        connect(m_btnTransport[i], &KeyButton::clicked, this, [this, i] { transportSelected(Transport(i)); });
    }
    m_btnTransport[0]->setRed(true);
    group(m_selCap = cap(T("Selected channels (%1)").arg(0)),
          {m_btnTransport[0], m_btnTransport[1], m_btnTransport[2], m_btnTransport[3]});

    // Zaznaczenie i akcje na zaznaczonych - male przyciski w jednym wierszu
    const char* const selNames[5] = {"ALL", "NONE", "INVERT", "DUPLICATE", "REMOVE"};
    for (int i = 0; i < 5; ++i) {
        m_btnSelect[i] = new KeyButton(T(selNames[i]), KeyButton::None, false);
        m_btnSelect[i]->setCompact();
    }
    connect(m_btnSelect[0], &KeyButton::clicked, m_mgr, &ChannelManager::selectAll);
    connect(m_btnSelect[1], &KeyButton::clicked, m_mgr, &ChannelManager::selectNone);
    connect(m_btnSelect[2], &KeyButton::clicked, m_mgr, &ChannelManager::invertSelection);
    connect(m_btnSelect[3], &KeyButton::clicked, this, [this] { m_mgr->duplicateChannels(m_mgr->selected()); });
    connect(m_btnSelect[4], &KeyButton::clicked, this, &MainWindow::removeSelected);
    group(cap(T("Selection")), {m_btnSelect[0], m_btnSelect[1], m_btnSelect[2], m_btnSelect[3], m_btnSelect[4]});

    // Kanal aktywny (pomaranczowa ramka, pokazany w inspektorze)
    m_btnUp = new KeyButton(T("MOVE UP"), KeyButton::Up, false);
    m_btnDown = new KeyButton(T("MOVE DOWN"), KeyButton::Down, false);
    connect(m_btnUp, &KeyButton::clicked, this, [this] { m_mgr->moveActive(-1); });
    connect(m_btnDown, &KeyButton::clicked, this, [this] { m_mgr->moveActive(1); });
    group(cap(T("Active channel")), {m_btnUp, m_btnDown});

    // Lista kanalow jako calosc; licznik n / 16 w wierszu tytulu
    m_btnAdd = new KeyButton(T("ADD SOURCE"), KeyButton::None, true);
    auto* load = new KeyButton(T("LOAD"), KeyButton::None, true);
    auto* save = new KeyButton(T("SAVE"), KeyButton::None, true);
    load->setEnabled(false);
    save->setEnabled(false);
    load->setToolTip(T("From version %1").arg(QLatin1String(Roadmap::lists)));
    save->setToolTip(T("From version %1").arg(QLatin1String(Roadmap::lists)));
    connect(m_btnAdd, &KeyButton::clicked, m_mgr, [this] { m_mgr->addChannel(); });
    m_count = label(QString(), "countN");
    m_count->setTextFormat(Qt::RichText);
    group(cap(T("Channel list")), {m_btnAdd, load, save}, m_count);

    return panel;
}

QWidget* MainWindow::buildStatus() {
    auto* st = new QWidget;
    st->setObjectName(QStringLiteral("status"));
    st->setAttribute(Qt::WA_StyledBackground);
    st->setFixedHeight(StatusH);
    auto* h = new QHBoxLayout(st);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(22);
    auto* autosave = new QLabel(QStringLiteral("<b style='color:#9d9d9d'>Autosave</b> — ") + T("(from version %1)").arg(QLatin1String(Roadmap::lists)));
    autosave->setTextFormat(Qt::RichText);
    h->addWidget(autosave);
    h->addWidget(m_events = new QLabel, 1);
    h->addWidget(m_hint = new QLabel);
    // stany kanalow: REC / PAUSED / PREVIEW / WAITING (+ ERROR)
    h->addWidget(m_legend = new QLabel);
    m_legend->setTextFormat(Qt::RichText);
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
    m_btnSelect[3]->setEnabled(sel > 0 && m_mgr->canAdd());
    m_btnSelect[4]->setEnabled(sel > 0);
    m_btnAdd->setEnabled(m_mgr->canAdd());
    m_count->setText(QStringLiteral("%1<span style='color:#6e6e6e'> / %2</span>").arg(n).arg(ChannelManager::MaxChannels));

    int rec = 0, pau = 0, prv = 0, wait = 0, err = 0;
    for (Channel* c : m_mgr->channels()) {
        switch (c->state()) {
        case ChannelState::Recording: case ChannelState::Reconnecting: case ChannelState::Finalizing: ++rec; break;
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
    bool selRec = false;
    for (Channel* c : m_mgr->selected()) selRec = selRec || c->isRecording();
    m_btnTransport[0]->setLit(selRec);
    m_hint->setText(m_mgr->canAdd() ? QString() : T("Limit of 16 channels reached — adding sources is disabled"));
}

// Naglowek: REC przy kanale aktywnym, laczny strumien zapisu, liczba sesji enkoderow sprzetowych.
void MainWindow::refreshRecording() {
    const Channel* active = m_mgr->active();
    m_recBadge->setVisible(active && active->state() == ChannelState::Recording);
    double mbps = 0;
    int hw = 0;
    for (const Channel* c : m_mgr->channels()) {
        if (!c->isRecording()) continue;
        mbps += c->recordMbps();
        hw += c->encoderIsHardware() ? 1 : 0;
    }
    m_write->setText(QStringLiteral("%1 Mb/s").arg(qRound(mbps)));
    m_hwEnc->setText(QString::number(hw));
    if (m_closing && m_mgr->recordingCount() == 0) close();
}

void MainWindow::transportSelected(Transport t) {
    if (t == Transport::Stop) {      // ochrona przed przypadkowym zatrzymaniem wielu nagran
        int rec = 0;
        for (Channel* c : m_mgr->selected()) rec += c->state() == ChannelState::Recording ? 1 : 0;
        if (rec >= 2 && QMessageBox::question(this, QStringLiteral("ISO Recorder"),
                                              T("Stop recording on %1 channels?").arg(rec)) != QMessageBox::Yes)
            return;
    }
    m_mgr->transportSelected(t);
}

void MainWindow::removeSelected() {
    const QList<Channel*> sel = m_mgr->selected();
    if (ChannelTile::confirmRemove(this, sel)) m_mgr->removeChannels(sel);
}

// Zamkniecie okna w trakcie nagrywania: najpierw finalizacja wszystkich plikow, dopiero potem wyjscie.
void MainWindow::closeEvent(QCloseEvent* e) {
    if (m_mgr->recordingCount() == 0) {
        e->accept();
        return;
    }
    e->ignore();
    if (!m_closing) {
        m_closing = true;
        notice(T("Finishing recordings before exit…"));
        m_mgr->stopAllRecording();
    }
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
    // Szerokosc prawej kolumny wynika z proporcji siatki: blok wideo ma dokladnie taki ksztalt,
    // zeby 4 x 4 kafle 16:9 wypelnily go z rownymi marginesami. Reszta szerokosci to prawa kolumna.
    const int gridH = height() - HeaderH - StatusH;
    m_right->setFixedWidth(qBound(RightMin, width() - ChannelGrid::widthForHeight(gridH, 4), RightMax));
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    Channel* ch = m_mgr->active();
    const int i = m_mgr->indexOf(ch), n = m_mgr->count();
    switch (e->key()) {
    case Qt::Key_A: if (ctrl && shift) m_mgr->selectNone(); else if (ctrl) m_mgr->selectAll(); break;
    case Qt::Key_I: if (ctrl) m_mgr->invertSelection(); break;
    case Qt::Key_Delete:
        if (shift) removeSelected();
        else if (ch && ChannelTile::confirmRemove(this, {ch})) m_mgr->removeChannel(ch);
        break;
    case Qt::Key_D: if (ctrl) m_mgr->duplicateChannels(m_mgr->selected()); break;
    case Qt::Key_R: if (ch && !ctrl) m_mgr->transport(ch, Transport::Rec); break;
    case Qt::Key_S: if (ch && !ctrl) m_mgr->transport(ch, Transport::Stop); break;
    case Qt::Key_Up: if (ctrl) m_mgr->moveActive(-1); break;
    case Qt::Key_Down: if (ctrl) m_mgr->moveActive(1); break;
    case Qt::Key_Left: if (i > 0) m_mgr->setActive(m_mgr->channels().at(i - 1)); break;
    case Qt::Key_Right: if (i >= 0 && i < n - 1) m_mgr->setActive(m_mgr->channels().at(i + 1)); break;
    case Qt::Key_Space: if (ch) ch->setSelected(!ch->isSelected()); break;
    default: QWidget::keyPressEvent(e);
    }
}
