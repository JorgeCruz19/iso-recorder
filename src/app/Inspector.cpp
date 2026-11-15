#include "Inspector.h"

#include <QButtonGroup>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QDateEdit>
#include <QListWidget>
#include <QLocale>
#include <QTimeEdit>
#include <QMessageBox>
#include <QPointer>
#include <QRegularExpression>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "SettingsDialog.h"
#include "Widgets.h"
#include "core/AppSettings.h"
#include "core/ChannelManager.h"
#include "core/DiskMonitor.h"
#include "core/Lang.h"
#include <QStorageInfo>
#include <QTimer>
#include "media/Devices.h"
#include "media/EncoderFactory.h"
#include "util/FileNameTemplate.h"

namespace {
QLabel* section(const QString& text, bool collapsed = false) {
    auto* l = new QLabel((collapsed ? QStringLiteral("►  ") : QStringLiteral("▼  ")) + text);
    l->setObjectName(collapsed ? QStringLiteral("shOff") : QStringLiteral("sh"));
    return l;
}

QGridLayout* form(QVBoxLayout* into) {
    auto* g = new QGridLayout;
    g->setContentsMargins(12, 8, 12, 8);
    g->setHorizontalSpacing(12);
    g->setVerticalSpacing(8);
    g->setColumnMinimumWidth(0, 104);
    g->setColumnStretch(1, 1);
    into->addLayout(g);
    return g;
}

QLabel* row(QGridLayout* g, const QString& label, QWidget* field) {
    const int r = g->rowCount();
    auto* l = new QLabel(label);
    l->setObjectName(QStringLiteral("fl"));
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setWordWrap(true);                   // dluga etykieta (tlumaczenia) lamie sie na dwie linie zamiast byc obcieta
    l->setFixedWidth(104);
    g->addWidget(l, r, 0);
    g->addWidget(field, r, 1);
    return l;
}

QWidget* placeholder(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("note"));
    l->setWordWrap(true);
    l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return l;
}
}

Inspector::Inspector(ChannelManager* mgr, QWidget* parent) : QWidget(parent), m_mgr(mgr) {
    setObjectName(QStringLiteral("inspector"));
    setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(1, 0, 0, 0);
    v->setSpacing(0);

    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("ih"));
    m_title->setTextFormat(Qt::RichText);
    v->addWidget(m_title);
    m_preview = new PreviewPane;
    v->addWidget(m_preview);
    m_meters = new MeterPane;
    v->addWidget(m_meters);

    auto* tabs = new QHBoxLayout;
    tabs->setSpacing(0);
    auto* group = new QButtonGroup(this);
    const char* const names[3] = {"Input", "Output", "Scheduler"};
    m_stack = new QStackedWidget;
    for (int i = 0; i < 3; ++i) {
        m_tabs[i] = new QPushButton(QLatin1String(names[i]));
        m_tabs[i]->setObjectName(QStringLiteral("itab"));
        m_tabs[i]->setCheckable(true);
        m_tabs[i]->setFocusPolicy(Qt::NoFocus);
        group->addButton(m_tabs[i], i);
        tabs->addWidget(m_tabs[i]);
    }
    m_tabs[0]->setChecked(true);
    connect(group, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);
    v->addLayout(tabs);

    m_stack->addWidget(m_inputPage = buildInputPage());
    m_stack->addWidget(buildOutputPage());
    m_stack->addWidget(buildSchedulePage());
    // Prawa kolumna jest waska i dzieli wysokosc z panelem sterowania - tresc zakladek przewija sie.
    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_stack);
    v->addWidget(scroll, 1);

    // Listy rozwijane maja szerokosc kolumny, a nie najdluzszej pozycji: dlugie nazwy urzadzen rozpychaly formularz
    // poza krawedz okna i obcinaly etykiety. Tekst jest skracany wielokropkiem, pelna nazwa jest w podpowiedzi,
    // a rozwinieta lista ma najwyzej 12 pozycji i szerokosc pola.
    for (QComboBox* c : findChildren<QComboBox*>()) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(6);
        c->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        c->setMaxVisibleItems(12);
        c->view()->setTextElideMode(Qt::ElideMiddle);
        c->view()->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        for (int i = 0; i < c->count(); ++i) c->setItemData(i, c->itemText(i), Qt::ToolTipRole);
        connect(c, &QComboBox::currentTextChanged, c, [c](const QString& text) { c->setToolTip(text); });
        c->setToolTip(c->currentText());
    }
    for (QLineEdit* e : findChildren<QLineEdit*>())
        if (!qobject_cast<QAbstractSpinBox*>(e->parentWidget())) e->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(mgr, &ChannelManager::activeChanged, this, &Inspector::bind);
    connect(mgr, &ChannelManager::listChanged, this, &Inspector::bind);
    bind();
}

void Inspector::setTab(int index) {
    if (index < 0 || index > 2) return;
    m_tabs[index]->setChecked(true);
    m_stack->setCurrentIndex(index);
}

QWidget* Inspector::buildInputPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    v->addWidget(section(T("Source")));
    QGridLayout* g = form(v);
    m_name = new QLineEdit;
    row(g, T("Channel name"), m_name);
    m_type = new QComboBox;
    m_type->addItem(T("None (input off)"), QStringLiteral("none"));
    m_type->addItem(T("Test generator"), QStringLiteral("test"));
    m_type->addItem(T("Screen grab"), QStringLiteral("screen"));
    m_type->addItem(T("Window capture"), QStringLiteral("window"));
    m_type->addItem(T("Camera (UVC, capture card)"), QStringLiteral("camera"));
    m_type->addItem(QStringLiteral("HDMI / SDI — Blackmagic DeckLink"), QStringLiteral("decklink"));
    m_type->addItem(QStringLiteral("SRT"), QStringLiteral("srt"));
    m_type->addItem(QStringLiteral("RTSP"), QStringLiteral("rtsp"));
    m_type->addItem(QStringLiteral("NDI"), QStringLiteral("ndi"));
    m_type->addItem(QStringLiteral("WebRTC (WHEP)"), QStringLiteral("whep"));
    row(g, T("Input type"), m_type);
    m_device = new QComboBox;
    row(g, T("Device"), m_device);
    // zrodla NDI pojawiaja sie w sieci i znikaja - lista odswieza sie sama, gdy nie jest rozwinieta
    auto* ndiTimer = new QTimer(this);
    connect(ndiTimer, &QTimer::timeout, this, [this] {
        Channel* ch = m_mgr->active();
        if (!ch || ch->config().in.type != QLatin1String("ndi") || m_device->view()->isVisible()) return;
        QStringList now, shown;
        for (const Devices::Info& d : Devices::ndiSources()) now << d.id;
        for (int i = 0; i < m_device->count(); ++i) shown << m_device->itemData(i).toStringList().value(0);
        if (!ch->config().in.uri.isEmpty() && !now.contains(ch->config().in.uri)) now << ch->config().in.uri;
        if (now == shown) return;
        m_device->setProperty("forType", QString());
        refresh();
    });
    ndiTimer->start(2000);
    QTimer::singleShot(0, this, [] { Devices::ndiSources(); });     // wyszukiwanie zrodel NDI rusza od razu
    m_uri = new QLineEdit;
    row(g, T("Address"), m_uri);
    m_camMode = new QComboBox;
    row(g, T("Camera mode"), m_camMode);
    // Co robic z nagraniem, gdy zrodlo sieciowe straci sygnal
    m_loss = new QComboBox;
    m_loss->addItem(T("Keep recording (black picture)"), QStringLiteral("continue"));
    m_loss->addItem(T("Pause until the signal returns"), QStringLiteral("pause"));
    m_loss->addItem(T("New file part after the signal returns"), QStringLiteral("split"));
    row(g, T("On signal loss"), m_loss);
    m_pattern = new QComboBox;
    const struct { const char* name; int id; } patterns[] = {
        {"SMPTE bars", 0}, {"SMPTE bars 75%", 13}, {"SMPTE bars 100%", 19}, {"Ball", 18}, {"Pinwheel", 21},
        {"Spokes", 22}, {"Circles", 11}, {"Gradient", 23}, {"Snow", 1}, {"Black", 2}};
    for (const auto& pt : patterns) m_pattern->addItem(T(pt.name), pt.id);
    m_pattern->addItem(T("Digital clock"), Channel::ClockDigital);
    m_pattern->addItem(T("Analog clock"), Channel::ClockAnalog);
    row(g, T("Pattern"), m_pattern);
    m_format = new QComboBox;
    const struct { int w, h; const char* ratio; } formats[] = {
        {1280, 720, "16:9"}, {1920, 1080, "16:9"}, {1024, 768, "4:3"}, {720, 1280, "9:16"}, {1920, 800, "2.40:1"}, {1080, 1080, "1:1"}};
    for (const auto& f : formats)
        m_format->addItem(QStringLiteral("%1 × %2  ·  %3").arg(f.w).arg(f.h).arg(QLatin1String(f.ratio)), QSize(f.w, f.h));
    row(g, T("Video format"), m_format);
    m_fps = new QComboBox;
    for (int f : {24, 25, 30, 50, 60}) m_fps->addItem(T("%1 fps").arg(f), f);
    row(g, T("Frame rate"), m_fps);
    m_reconnect = new QPushButton(T("Reconnect source"));
    m_reconnect->setObjectName(QStringLiteral("ibtn"));
    m_reconnect->setToolTip(T("Stops the source and starts it again. Use it when a camera or a capture card shows no picture."));
    g->addWidget(m_reconnect, g->rowCount(), 1);

    v->addWidget(section(QStringLiteral("Audio")));
    g = form(v);
    // Zrodlo dzwieku: sygnaly testowe, dzwiek systemowy (loopback) i wejscia WASAPI.
    // Dane pozycji: [typ, id urzadzenia, nazwa, ksztalt sygnalu testowego].
    m_wave = new QComboBox;
    auto audioItem = [this](const QString& text, const QString& type, const QString& id = {}, const QString& name = {}, int wave = 0) {
        m_wave->addItem(text, QStringList{type, id, name, QString::number(wave)});
    };
    audioItem(T("Sine 440 Hz"), QStringLiteral("test"), {}, {}, 0);
    audioItem(T("Ticks every 1 s"), QStringLiteral("test"), {}, {}, 8);
    audioItem(T("Silence"), QStringLiteral("none"));
    audioItem(T("System sound (default output)"), QStringLiteral("loopback"));
    for (const Devices::Info& d : Devices::list().audioLoopbacks)
        audioItem(T("System sound: %1").arg(d.name), QStringLiteral("loopback"), d.id, d.name);
    audioItem(T("Microphone / line in (default input)"), QStringLiteral("device"));
    for (const Devices::Info& d : Devices::list().audioInputs)
        audioItem(T("Input: %1").arg(d.name), QStringLiteral("device"), d.id, d.name);
    row(g, T("Audio source"), m_wave);
    m_gain = new QComboBox;
    for (int db = 18; db >= -24; db -= 3) m_gain->addItem(db == 0 ? QStringLiteral("0 dB") : QStringLiteral("%1%2 dB").arg(db > 0 ? QStringLiteral("+") : QStringLiteral("−")).arg(qAbs(db)), db);
    row(g, T("Input gain"), m_gain);

    v->addStretch(1);

    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        if (Channel* ch = m_mgr->active()) ch->setName(m_name->text());
    });
    connect(m_pattern, &QComboBox::activated, this, [this](int i) {
        if (Channel* ch = m_mgr->active()) ch->setPattern(m_pattern->itemData(i).toInt());
    });
    connect(m_format, &QComboBox::activated, this, [this](int i) {
        const QSize s = m_format->itemData(i).toSize();
        if (Channel* ch = m_mgr->active()) ch->setFormat(s.width(), s.height());
    });
    connect(m_fps, &QComboBox::activated, this, [this](int i) {
        if (Channel* ch = m_mgr->active()) ch->setFps(m_fps->itemData(i).toInt());
    });
    connect(m_reconnect, &QPushButton::clicked, this, [this] {
        if (Channel* ch = m_mgr->active()) ch->reconnect();
    });
    connect(m_wave, &QComboBox::activated, this, &Inspector::commitInput);
    connect(m_device, &QComboBox::activated, this, &Inspector::commitInput);
    connect(m_uri, &QLineEdit::editingFinished, this, &Inspector::commitInput);
    for (QComboBox* c : {m_camMode, m_loss, m_gain}) connect(c, &QComboBox::activated, this, &Inspector::commitInput);
    connect(m_type, &QComboBox::activated, this, [this](int i) {
        fillDevices(m_type->itemData(i).toString());
        commitInput();
    });
    return page;
}

QWidget* Inspector::buildOutputPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_lock = new QLabel(T("Recording settings are locked while recording"));
    m_lock->setObjectName(QStringLiteral("lock"));
    v->addWidget(m_lock);
    m_outputForm = new QWidget;
    auto* fv = new QVBoxLayout(m_outputForm);
    fv->setContentsMargins(0, 0, 0, 0);
    fv->setSpacing(0);
    v->addWidget(m_outputForm);

    fv->addWidget(section(T("Destination")));
    QGridLayout* g = form(fv);
    auto* dirRow = new QWidget;
    auto* dh = new QHBoxLayout(dirRow);
    dh->setContentsMargins(0, 0, 0, 0);
    dh->setSpacing(4);
    m_outDir = new QLineEdit;
    m_outDir->setPlaceholderText(QDir::toNativeSeparators(Channel::defaultOutputDir()));
    auto* browse = new QPushButton(QStringLiteral("…"));
    browse->setObjectName(QStringLiteral("ibtn"));
    browse->setFixedWidth(30);
    dh->addWidget(m_outDir, 1);
    dh->addWidget(browse);
    row(g, T("Folder"), dirRow);
    // Druga lokalizacja: folder zapasowy (gdy na dysku brakuje miejsca) albo zapis do dwoch folderow naraz
    m_outDir2Mode = new QComboBox;
    m_outDir2Mode->addItem(T("Off"), QStringLiteral("off"));
    m_outDir2Mode->addItem(T("Spare folder when the disk fills up"), QStringLiteral("spare"));
    m_outDir2Mode->addItem(T("Record to both folders at once"), QStringLiteral("mirror"));
    const QString secondHelp = T("Spare folder: when less free space than the limit is left on the disk of the main folder, the "
                                 "recording goes on in the second folder as a new file part. Both folders: the same recording is "
                                 "written to two folders at once; if the second disk fails, the recording continues in the main folder.");
    row(g, T("Second folder"), m_outDir2Mode)->setToolTip(secondHelp);
    m_outDir2Mode->setToolTip(secondHelp);
    auto* dir2Row = new QWidget;
    auto* dh2 = new QHBoxLayout(dir2Row);
    dh2->setContentsMargins(0, 0, 0, 0);
    dh2->setSpacing(4);
    m_outDir2 = new QLineEdit;
    m_outDir2Browse = new QPushButton(QStringLiteral("…"));
    m_outDir2Browse->setObjectName(QStringLiteral("ibtn"));
    m_outDir2Browse->setFixedWidth(30);
    dh2->addWidget(m_outDir2, 1);
    dh2->addWidget(m_outDir2Browse);
    row(g, T("Folder 2"), dir2Row);
    m_outSpare = new QComboBox;
    for (int gb : {1, 2, 5, 10, 20, 50, 100}) m_outSpare->addItem(T("below %1 GB free").arg(gb), gb * 1024);
    row(g, T("Switch"), m_outSpare);
    m_outTemplate = new QLineEdit;
    m_outTemplate->setPlaceholderText(QLatin1String(FileNameTemplate::kDefault));
    // spis zmiennych w podpowiedzi - po najechaniu na etykiete i na pole
    row(g, T("Name template"), m_outTemplate)->setToolTip(nameTemplateHelp());
    m_outTemplate->setToolTip(nameTemplateHelp());
    m_outContainer = new QComboBox;
    fillContainerCombo(m_outContainer);
    row(g, T("Container"), m_outContainer)->setToolTip(containerHelp());
    // Auto-split: dane = sekundy (dodatnie) albo bajty zapisane jako liczba ujemna
    m_outSplit = new QComboBox;
    m_outSplit->addItem(T("Off"), qlonglong(0));
    for (int min : {1, 5, 10, 15, 30, 60}) m_outSplit->addItem(T("every %1 min").arg(min), qlonglong(min * 60));
    for (int gbs : {1, 2, 4}) m_outSplit->addItem(T("every %1 GB").arg(gbs), qlonglong(-(qlonglong(gbs) << 30)));
    row(g, T("Auto-split"), m_outSplit);

    fv->addWidget(section(T("Video")));
    g = form(fv);
    m_outCodec = new QComboBox;
    fillCodecCombo(m_outCodec);
    row(g, T("Codec"), m_outCodec);
    m_outEncoder = new QComboBox;
    fillEncoderCombo(m_outEncoder, QStringLiteral("h264"));
    m_outEncoder->setToolTip(T("No re-encoding (stream copy)") + QStringLiteral(": SRT / RTSP. ") + T("Set by the source (stream copy)") +
                             QStringLiteral(": ") + T("Bitrate"));
    row(g, T("Encoder"), m_outEncoder);
    m_outVideoRate = new QComboBox;
    for (int mb : {2, 4, 6, 8, 12, 20, 35, 50}) m_outVideoRate->addItem(QStringLiteral("%1 Mb/s").arg(mb), mb * 1000);
    row(g, T("Bitrate"), m_outVideoRate);

    fv->addWidget(section(QStringLiteral("Audio")));
    g = form(fv);
    m_outAudioRate = new QComboBox;
    for (int kb : {128, 192, 256, 320}) m_outAudioRate->addItem(QStringLiteral("AAC  %1 kb/s").arg(kb), kb);
    row(g, T("Bitrate"), m_outAudioRate);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(12, 6, 12, 10);
    m_applySelected = new QPushButton(T("Apply to selected channels"));
    m_applySelected->setObjectName(QStringLiteral("ibtn"));
    bar->addWidget(m_applySelected);
    fv->addLayout(bar);

    // Wyjscie na zywo: rownolegle do nagrywania (i bez niego) kanal idzie na NDI albo SRT.
    // Sekcja lezy poza formularzem blokowanym podczas nagrywania - wyjscie mozna zmieniac w kazdej chwili.
    v->addWidget(section(T("Live output")));
    g = form(v);
    m_liveType = new QComboBox;
    m_liveType->addItem(T("Off"), QStringLiteral("off"));
    m_liveType->addItem(QStringLiteral("NDI"), QStringLiteral("ndi"));
    m_liveType->addItem(QStringLiteral("SRT  (MPEG-TS, H.264 + AAC)"), QStringLiteral("srt"));
    row(g, T("Send to"), m_liveType);
    m_liveNdi = new QLineEdit;
    row(g, T("NDI name"), m_liveNdi);
    m_liveSrt = new QLineEdit;
    m_liveSrt->setPlaceholderText(QStringLiteral(":9001"));
    m_liveSrt->setToolTip(T(":9001 — receivers connect to this computer on port 9001;  host:9001 — this computer connects to the receiver"));
    row(g, T("SRT address"), m_liveSrt);
    m_liveRate = new QComboBox;
    for (int mb : {2, 4, 6, 8, 12, 20}) m_liveRate->addItem(QStringLiteral("%1 Mb/s").arg(mb), mb * 1000);
    row(g, T("Stream bitrate"), m_liveRate);
    m_liveWhen = new QComboBox;
    m_liveWhen->addItem(T("All the time"), QStringLiteral("always"));
    m_liveWhen->addItem(T("As in the recording schedule"), QStringLiteral("schedule"));
    m_liveWhen->setToolTip(T("As in the recording schedule: the output is on only inside the time windows of the Scheduler tab."));
    row(g, T("Active"), m_liveWhen);
    connect(m_liveWhen, &QComboBox::activated, this, &Inspector::commitLive);
    connect(m_liveType, &QComboBox::activated, this, &Inspector::commitLive);
    connect(m_liveRate, &QComboBox::activated, this, &Inspector::commitLive);
    connect(m_liveNdi, &QLineEdit::editingFinished, this, &Inspector::commitLive);
    connect(m_liveSrt, &QLineEdit::editingFinished, this, &Inspector::commitLive);
    v->addStretch(1);

    connect(m_outDir, &QLineEdit::editingFinished, this, &Inspector::commitOutput);
    connect(m_outDir2, &QLineEdit::editingFinished, this, &Inspector::commitOutput);
    connect(m_outDir2Mode, &QComboBox::activated, this, &Inspector::commitOutput);
    connect(m_outSpare, &QComboBox::activated, this, &Inspector::commitOutput);
    connect(m_outDir2Browse, &QPushButton::clicked, this, [this] {
        Channel* ch = m_mgr->active();
        if (!ch) return;
        const QString start = ch->config().out.dir2.isEmpty() ? ch->outputDir() : ch->config().out.dir2;
        const QString dir = QFileDialog::getExistingDirectory(this, T("Choose recording folder"), start);
        if (dir.isEmpty()) return;
        m_outDir2->setText(QDir::toNativeSeparators(dir));
        commitOutput();
    });
    connect(m_outTemplate, &QLineEdit::editingFinished, this, &Inspector::commitOutput);
    for (QComboBox* c : {m_outCodec, m_outEncoder, m_outVideoRate, m_outAudioRate, m_outContainer, m_outSplit})
        connect(c, &QComboBox::activated, this, &Inspector::commitOutput);
    connect(browse, &QPushButton::clicked, this, [this] {
        Channel* ch = m_mgr->active();
        if (!ch) return;
        const QString dir = QFileDialog::getExistingDirectory(this, T("Choose recording folder"), ch->outputDir());
        if (dir.isEmpty()) return;
        m_outDir->setText(QDir::toNativeSeparators(dir));
        commitOutput();
    });
    // Te same ustawienia zapisu dla wielu kanalow: kopiuje Output kanalu aktywnego na zaznaczone (poza nagrywajacymi).
    connect(m_applySelected, &QPushButton::clicked, this, [this] {
        Channel* src = m_mgr->active();
        if (!src) return;
        int n = 0;
        for (Channel* c : m_mgr->selected()) {
            if (c == src || !c->canEdit()) continue;
            c->setOutput(src->config().out);
            ++n;
        }
        emit m_mgr->notice(T("Output settings copied to %1 channels").arg(n));
    });
    connect(m_mgr, &ChannelManager::selectionChanged, this, [this] {
        m_applySelected->setEnabled(m_mgr->active() && m_mgr->selectedCount() > 0);
    });
    return page;
}

// Zakladka Scheduler: lista regul kanalu aktywnego i edytor zaznaczonej reguly. Kazda zmiana trafia do kanalu od razu.
// Reguly mozna zmieniac takze podczas nagrywania.
QWidget* Inspector::buildSchedulePage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_schedStatus = new QLabel;
    m_schedStatus->setObjectName(QStringLiteral("schedStatus"));
    m_schedStatus->setWordWrap(true);
    v->addWidget(m_schedStatus);

    v->addWidget(section(T("Recording rules")));
    auto* lv = new QVBoxLayout;
    lv->setContentsMargins(12, 8, 12, 8);
    lv->setSpacing(6);
    m_ruleList = new QListWidget;
    m_ruleList->setFixedHeight(112);
    m_ruleList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_ruleList->setTextElideMode(Qt::ElideRight);
    m_ruleList->setToolTip(T("Each rule is a time window: the channel starts recording at its beginning and stops at its end. "
                             "The box switches a rule on and off."));
    lv->addWidget(m_ruleList);
    auto* bh = new QHBoxLayout;
    bh->setSpacing(6);
    m_ruleAdd = new QPushButton(T("Add rule"));
    m_ruleDel = new QPushButton(T("Remove rule"));
    for (QPushButton* b : {m_ruleAdd, m_ruleDel}) {
        b->setObjectName(QStringLiteral("ibtn"));
        bh->addWidget(b);
    }
    bh->addStretch(1);
    lv->addLayout(bh);
    v->addLayout(lv);

    v->addWidget(section(T("Selected rule")));
    m_ruleEditor = new QWidget;
    auto* ev = new QVBoxLayout(m_ruleEditor);
    ev->setContentsMargins(0, 0, 0, 0);
    QGridLayout* g = form(ev);
    m_ruleRepeat = new QComboBox;
    m_ruleRepeat->addItem(T("Every week, on chosen days"), true);
    m_ruleRepeat->addItem(T("Once, on a chosen date"), false);
    row(g, T("Repeat"), m_ruleRepeat);
    auto* daysRow = new QWidget;
    auto* dh = new QHBoxLayout(daysRow);
    dh->setContentsMargins(0, 0, 0, 0);
    dh->setSpacing(2);
    const QLocale loc(Lang::current());
    for (int d = 0; d < 7; ++d) {
        m_ruleDays[d] = new QPushButton(loc.standaloneDayName(d + 1, QLocale::ShortFormat).left(2).toUpper());
        m_ruleDays[d]->setObjectName(QStringLiteral("day"));
        m_ruleDays[d]->setCheckable(true);
        m_ruleDays[d]->setFocusPolicy(Qt::NoFocus);
        m_ruleDays[d]->setToolTip(loc.standaloneDayName(d + 1, QLocale::LongFormat));
        m_ruleDays[d]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        dh->addWidget(m_ruleDays[d], 1);
        connect(m_ruleDays[d], &QPushButton::clicked, this, &Inspector::commitSchedule);
    }
    row(g, T("Days"), daysRow);
    m_ruleDate = new QDateEdit;
    m_ruleDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    row(g, T("Date"), m_ruleDate);
    m_ruleStart = new QTimeEdit;
    m_ruleStop = new QTimeEdit;
    for (QTimeEdit* t : {m_ruleStart, m_ruleStop}) t->setDisplayFormat(QStringLiteral("HH:mm:ss"));
    row(g, T("Start at"), m_ruleStart);
    row(g, T("Stop at"), m_ruleStop)->setToolTip(T("A stop time earlier than the start time means the next day."));
    m_ruleStop->setToolTip(T("A stop time earlier than the start time means the next day."));
    m_ruleSplit = new QComboBox;
    m_ruleSplit->addItem(T("Off"), 0);
    for (int min : {1, 5, 10, 15, 30, 60}) m_ruleSplit->addItem(T("every %1 min").arg(min), min * 60);
    row(g, T("New file part"), m_ruleSplit);
    v->addWidget(m_ruleEditor);

    v->addWidget(section(T("Options")));
    auto* ov = new QVBoxLayout;
    ov->setContentsMargins(12, 8, 12, 8);
    m_catchUp = new QCheckBox(T("Start late when the start time was missed"));
    m_catchUp->setToolTip(T("When the program was not running or the computer was asleep at the start time, recording begins as soon "
                            "as possible inside the window. Off: that window is skipped."));
    ov->addWidget(m_catchUp);
    v->addLayout(ov);
    v->addStretch(1);

    connect(m_ruleAdd, &QPushButton::clicked, this, [this] {
        Channel* ch = m_mgr->active();
        if (!ch || ch->config().sched.rules.size() >= 32) return;
        ScheduleConfig sc = ch->config().sched;
        ScheduleRule r;
        r.date = QDate::currentDate();
        const QTime now = QTime::currentTime();
        r.start = QTime((now.hour() + 1) % 24, 0);          // najblizsza pelna godzina, okno 1 h
        r.stop = QTime((now.hour() + 2) % 24, 0);
        sc.rules.append(r);
        ch->setSchedule(sc);
        m_ruleList->setCurrentRow(int(sc.rules.size()) - 1);
    });
    connect(m_ruleDel, &QPushButton::clicked, this, [this] {
        Channel* ch = m_mgr->active();
        const int i = m_ruleList->currentRow();
        if (!ch || i < 0 || i >= ch->config().sched.rules.size()) return;
        ScheduleConfig sc = ch->config().sched;
        sc.rules.removeAt(i);
        ch->setSchedule(sc);
    });
    connect(m_ruleList, &QListWidget::currentRowChanged, this, [this] { fillRuleEditor(); });
    connect(m_ruleList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {      // pole wlaczenia reguly
        Channel* ch = m_mgr->active();
        const int i = m_ruleList->row(item);
        if (m_schedFilling || !ch || i < 0 || i >= ch->config().sched.rules.size()) return;
        ScheduleConfig sc = ch->config().sched;
        sc.rules[i].enabled = item->checkState() == Qt::Checked;
        ch->setSchedule(sc);
    });
    connect(m_ruleRepeat, &QComboBox::activated, this, &Inspector::commitSchedule);
    connect(m_ruleSplit, &QComboBox::activated, this, &Inspector::commitSchedule);
    connect(m_ruleDate, &QDateEdit::dateChanged, this, &Inspector::commitSchedule);
    connect(m_ruleStart, &QTimeEdit::timeChanged, this, &Inspector::commitSchedule);
    connect(m_ruleStop, &QTimeEdit::timeChanged, this, &Inspector::commitSchedule);
    connect(m_catchUp, &QCheckBox::clicked, this, &Inspector::commitSchedule);
    return page;
}

// "Pn-Pt 08:00-16:00 · co 30 min" albo "2026-12-24 18:00-18:30"
QString Inspector::ruleText(const ScheduleRule& r) const {
    const QLocale loc(Lang::current());
    QString when;
    if (!r.weekly) {
        when = r.date.toString(Qt::ISODate);
    } else if (r.days == 0x7F) {
        when = T("Every day");
    } else if (r.days == 0) {
        when = T("No day chosen");
    } else {
        // kolejne dni jako zakres ("Pn-Pt"), pozostale po przecinku
        QStringList parts;
        for (int d = 0; d < 7;) {
            if (!(r.days & (1 << d))) { ++d; continue; }
            int e = d;
            while (e + 1 < 7 && (r.days & (1 << (e + 1)))) ++e;
            const QString a = loc.standaloneDayName(d + 1, QLocale::ShortFormat), b = loc.standaloneDayName(e + 1, QLocale::ShortFormat);
            parts << (e - d >= 2 ? a + QStringLiteral("–") + b : e > d ? a + QStringLiteral(", ") + b : a);
            d = e + 1;
        }
        when = parts.join(QStringLiteral(", "));
    }
    auto hm = [](const QTime& t) { return t.toString(t.second() ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm")); };
    QString text = QStringLiteral("%1  %2–%3").arg(when, hm(r.start), hm(r.stop));
    if (r.splitSeconds > 0) text += QStringLiteral("  ·  ") + T("every %1 min").arg(r.splitSeconds / 60);
    return text;
}

void Inspector::fillRuleEditor() {
    Channel* ch = m_mgr->active();
    const int i = m_ruleList->currentRow();
    const bool have = ch && i >= 0 && i < ch->config().sched.rules.size();
    m_ruleEditor->setEnabled(have);
    m_ruleDel->setEnabled(have);
    if (!have) return;
    const ScheduleRule& r = ch->config().sched.rules.at(i);
    m_schedFilling = true;
    m_ruleRepeat->setCurrentIndex(r.weekly ? 0 : 1);
    for (int d = 0; d < 7; ++d) {
        m_ruleDays[d]->setChecked(r.days & (1 << d));
        m_ruleDays[d]->setEnabled(r.weekly);
    }
    m_ruleDate->setEnabled(!r.weekly);
    if (!m_ruleDate->hasFocus()) m_ruleDate->setDate(r.date.isValid() ? r.date : QDate::currentDate());
    if (!m_ruleStart->hasFocus()) m_ruleStart->setTime(r.start);
    if (!m_ruleStop->hasFocus()) m_ruleStop->setTime(r.stop);
    int si = m_ruleSplit->findData(r.splitSeconds);
    if (si < 0) {           // wartosc spoza listy (np. z pliku)
        m_ruleSplit->addItem(QStringLiteral("%1 s").arg(r.splitSeconds), r.splitSeconds);
        si = m_ruleSplit->count() - 1;
    }
    m_ruleSplit->setCurrentIndex(si);
    m_schedFilling = false;
}

void Inspector::commitSchedule() {
    Channel* ch = m_mgr->active();
    if (m_schedFilling || !ch) return;
    ScheduleConfig sc = ch->config().sched;
    sc.catchUp = m_catchUp->isChecked();
    const int i = m_ruleList->currentRow();
    if (i >= 0 && i < sc.rules.size()) {
        ScheduleRule& r = sc.rules[i];
        r.weekly = m_ruleRepeat->currentData().toBool();
        int days = 0;
        for (int d = 0; d < 7; ++d) days |= m_ruleDays[d]->isChecked() ? (1 << d) : 0;
        r.days = days;
        r.date = m_ruleDate->date();
        r.start = m_ruleStart->time();
        r.stop = m_ruleStop->time();
        r.splitSeconds = m_ruleSplit->currentData().toInt();
    }
    ch->setSchedule(sc);
}

void Inspector::refreshSchedule() {
    Channel* ch = m_mgr->active();
    if (!ch) {
        m_schedFor = nullptr;
        m_schedShown = ScheduleConfig{};
        m_schedFilling = true;
        m_ruleList->clear();
        m_schedFilling = false;
        m_schedStatus->setText(QString());
        fillRuleEditor();
        return;
    }
    const ScheduleConfig& sc = ch->config().sched;
    if (ch == m_schedFor && !(sc == m_schedShown) && sc.rules.size() == m_ruleList->count()) {
        // ta sama liczba regul: zmiana tresci - lista jest poprawiana w miejscu, zeby edytowane pole nie tracilo fokusu
        m_schedShown = sc;
        m_schedFilling = true;
        for (int i = 0; i < sc.rules.size(); ++i) {
            QListWidgetItem* item = m_ruleList->item(i);
            item->setText(ruleText(sc.rules.at(i)));
            item->setToolTip(item->text());
            item->setCheckState(sc.rules.at(i).enabled ? Qt::Checked : Qt::Unchecked);
        }
        m_catchUp->setChecked(sc.catchUp);
        m_schedFilling = false;
        fillRuleEditor();
    } else if (ch != m_schedFor || !(sc == m_schedShown)) {
        const int keep = ch == m_schedFor ? m_ruleList->currentRow() : 0;
        m_schedFor = ch;
        m_schedShown = sc;
        m_schedFilling = true;
        m_ruleList->clear();
        for (const ScheduleRule& r : sc.rules) {
            auto* item = new QListWidgetItem(ruleText(r), m_ruleList);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            item->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(item->text());
        }
        m_catchUp->setChecked(sc.catchUp);
        m_schedFilling = false;
        m_ruleList->setCurrentRow(sc.rules.isEmpty() ? -1 : qBound(0, keep, int(sc.rules.size()) - 1));
        fillRuleEditor();
    }
    m_ruleAdd->setEnabled(sc.rules.size() < 32);
    // status: co harmonogram zrobi teraz / jako nastepne
    auto when = [](const QDateTime& t) {
        return t.date() == QDate::currentDate() ? t.toString(QStringLiteral("HH:mm:ss")) : t.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    };
    QString status;
    if (!sc.active()) status = T("No active rules — this channel records only when you press REC.");
    else if (ch->scheduleUntil().isValid()) status = T("Recording by schedule until %1.").arg(when(ch->scheduleUntil()));
    else if (ch->scheduleNext().isValid() && ch->scheduleNext() <= QDateTime::currentDateTime())
        status = T("The scheduled start is waiting for the source.");
    else if (ch->scheduleNext().isValid()) status = T("Next scheduled start: %1.").arg(when(ch->scheduleNext()));
    else status = T("No further scheduled starts.");
    m_schedStatus->setText(status);
}

void Inspector::commitOutput() {
    Channel* ch = m_mgr->active();
    if (!ch || !ch->canEdit()) return;
    OutputConfig out = ch->config().out;
    out.dir = QDir::fromNativeSeparators(m_outDir->text().trimmed());
    out.dir2Mode = m_outDir2Mode->currentData().toString();
    out.dir2 = QDir::fromNativeSeparators(m_outDir2->text().trimmed());
    out.spareMinMb = m_outSpare->currentData().toInt();
    out.nameTemplate = m_outTemplate->text().trimmed();
    out.encoder = m_outEncoder->currentData().toString();
    out.codec = m_outCodec->currentData().toString();
    if (out.codec.isEmpty()) out.codec = QStringLiteral("h264");
    // enkoder innego kodeka nie ma sensu po zmianie kodeka - wraca "auto"
    if (out.encoder != QLatin1String("auto") && out.encoder != QLatin1String("copy") && !EncoderFactory::installed(out.codec).contains(out.encoder))
        out.encoder = QStringLiteral("auto");
    out.videoKbps = m_outVideoRate->currentData().toInt();
    out.audioKbps = m_outAudioRate->currentData().toInt();
    out.container = m_outContainer->currentData().toString();
    // kontener, ktory nie przyjmie kodeka, jest zamieniany na pasujacy (w kopii strumienia kodek ustala zrodlo)
    if (out.encoder != QLatin1String("copy")) out.container = EncoderFactory::containerFor(out.container, out.codec);
    const qlonglong split = m_outSplit->currentData().toLongLong();
    out.splitSeconds = split > 0 ? int(split) : 0;
    out.splitBytes = split < 0 ? -split : 0;
    const bool dirChanged = out.dir != ch->config().out.dir;
    ch->setOutput(out);
    if (dirChanged && QString::fromLatin1(QStorageInfo(DiskMonitor::rootFor(ch->outputDir())).fileSystemType())
                          .compare(QLatin1String("FAT32"), Qt::CaseInsensitive) == 0)
        emit m_mgr->notice(T("This folder is on a FAT32 disk: files are limited to 4 GB — enable auto-split"));
}

// Lista urzadzen dla wybranego typu wejscia (ekrany albo kamery); generator testowy nie ma urzadzen.
void Inspector::fillDevices(const QString& type) {
    const QSignalBlocker block(m_device);
    m_device->clear();
    if (type == QLatin1String("screen")) {
        for (const Devices::Info& d : Devices::list(true).screens)
            m_device->addItem(QStringLiteral("%1: %2  ·  %3").arg(d.id.toInt() + 1).arg(d.name, d.detail), QStringList{d.id, d.name});
    } else if (type == QLatin1String("camera")) {
        for (const Devices::Info& d : Devices::list(true).cameras) m_device->addItem(d.name, QStringList{d.id, d.name});
    } else if (type == QLatin1String("window")) {
        for (const Devices::Info& d : Devices::windows()) m_device->addItem(d.name, QStringList{d.name, d.name});
    } else if (type == QLatin1String("ndi")) {
        for (const Devices::Info& d : Devices::ndiSources()) m_device->addItem(d.name, QStringList{d.id, d.name});
    } else if (type == QLatin1String("decklink")) {
        const QList<Devices::Info> cards = Devices::decklinks();
        for (const Devices::Info& d : cards) m_device->addItem(QStringLiteral("%1: %2").arg(d.id.toInt() + 1).arg(d.name), QStringList{d.id, d.name});
        // karty nie zostaly wykryte (brak sterownika Desktop Video albo karty): wejscia mozna wskazac numerem
        if (cards.isEmpty())
            for (int i = 0; i < 4; ++i)
                m_device->addItem(QStringLiteral("DeckLink %1  ·  %2").arg(i + 1).arg(T("not detected")),
                                  QStringList{QString::number(i), QStringLiteral("DeckLink %1").arg(i + 1)});
    }
    for (int i = 0; i < m_device->count(); ++i) m_device->setItemData(i, m_device->itemText(i), Qt::ToolTipRole);
    m_device->setProperty("forType", type);
}

// Rozmiary obrazu wybranej kamery; pierwsza pozycja zostawia wybor sterownikowi.
void Inspector::fillCameraModes(const QString& cameraPath) {
    const QSignalBlocker block(m_camMode);
    m_camMode->clear();
    m_camMode->addItem(T("Auto"), QString());
    for (const Devices::Info& d : Devices::list().cameras)
        if (d.id == cameraPath)
            for (const QString& m : d.modes) {
                // "1920x1080@30000/1001" -> "1920 × 1080  ·  29.97 fps"
                QString text = m.section(QLatin1Char('@'), 0, 0).replace(QLatin1Char('x'), QStringLiteral(" × "));
                const QStringList rate = m.section(QLatin1Char('@'), 1).split(QLatin1Char('/'));
                if (rate.size() == 2 && rate.at(1).toInt() > 0) {
                    const double f = rate.at(0).toDouble() / rate.at(1).toDouble();
                    text += QStringLiteral("  ·  ") + T("%1 fps").arg(QString::number(f, 'f', f == qRound(f) ? 0 : 2));
                }
                m_camMode->addItem(text, m);
            }
    m_camMode->setProperty("forCamera", cameraPath);
}

void Inspector::commitInput() {
    Channel* ch = m_mgr->active();
    if (!ch) return;
    InputConfig in = ch->config().in;
    const InputConfig before = in;
    in.gainDb = m_gain->currentData().toInt();          // glosnosc i reakcja na zanik dzialaja takze podczas nagrywania
    in.lossPolicy = m_loss->currentData().toString();
    if (!ch->canEdit()) {
        ch->setInput(in);
        return;
    }
    in.type = m_type->currentData().toString();
    if (in.type.isEmpty()) in.type = QStringLiteral("test");
    const QStringList dev = m_device->currentData().toStringList();
    if (in.type == QLatin1String("screen")) in.screenIndex = dev.value(0).toInt();
    if (in.type == QLatin1String("camera")) {
        const bool sameCamera = in.cameraPath == dev.value(0);
        in.cameraPath = dev.value(0);
        in.cameraName = dev.value(1);
        in.cameraMode = sameCamera ? m_camMode->currentData().toString() : QString();
    }
    if (in.type == QLatin1String("window") && !dev.value(0).isEmpty()) in.windowTitle = dev.value(0);
    if (in.type == QLatin1String("decklink") && m_device->property("forType").toString() == QLatin1String("decklink")) {
        in.deckIndex = dev.value(0).toInt();
        in.deckName = dev.value(1);
    }
    if (in.type == QLatin1String("ndi")) {
        if (ch->config().in.type != QLatin1String("ndi")) in.uri.clear();      // adres SRT/RTSP nie jest nazwa zrodla NDI
        if (m_device->property("forType").toString() == QLatin1String("ndi") && !dev.value(0).isEmpty()) in.uri = dev.value(0);
    } else if (ch->config().in.type == QLatin1String("ndi")) {
        in.uri.clear();
    } else {
        in.uri = m_uri->text().trimmed();
    }
    const QStringList au = m_wave->currentData().toStringList();
    in.audioType = au.value(0, QStringLiteral("test"));
    in.audioDevice = au.value(1);
    in.audioName = au.value(2);
    if (in.audioType == QLatin1String("test")) ch->setAudioWave(au.value(3).toInt());
    ch->setInput(in);
    // Kanal o domyslnej nazwie (TEST n albo nadanej tu automatycznie) dostaje propozycje nazwy wg nowego zrodla.
    const bool sourceChanged = before.type != in.type || before.screenIndex != in.screenIndex || before.deckIndex != in.deckIndex || before.cameraPath != in.cameraPath ||
                               before.windowTitle != in.windowTitle || before.uri != in.uri;
    static const QRegularExpression defaultName(QStringLiteral("^TEST \\d+$"));
    const QString current = ch->config().name;
    if (!sourceChanged || !(ch->config().in == in) || (!defaultName.match(current).hasMatch() && m_autoNames.value(ch) != current)) return;
    const QString name = suggestedName(ch);
    if (name.isEmpty() || name == current) return;
    QPointer<Channel> guard(ch);
    QTimer::singleShot(0, this, [this, guard, name] {       // po zamknieciu listy rozwijanej
        if (guard) offerRename(guard, name);
    });
}

// "Typ - urzadzenie", np. "NDI - STUDIO (Cam 1)", "Screen - 1 DELL U2412M", "SRT - 192.168.1.20:9000".
// Dla generatora testowego i zrodla jeszcze niewybranego (pusty adres) propozycji nie ma.
QString Inspector::suggestedName(Channel* ch) const {
    const InputConfig& in = ch->config().in;
    const QStringList dev = m_device->currentData().toStringList();
    QString kind, what;
    if (in.type == QLatin1String("screen")) {
        kind = T("Screen");
        what = QStringLiteral("%1 %2").arg(in.screenIndex + 1).arg(dev.value(1));
    } else if (in.type == QLatin1String("camera")) {
        kind = T("Camera");
        what = in.cameraName;
    } else if (in.type == QLatin1String("window")) {
        kind = T("Window");
        what = in.windowTitle;
    } else if (in.type == QLatin1String("ndi")) {
        kind = QStringLiteral("NDI");
        what = in.uri;
    } else if (in.type == QLatin1String("decklink")) {
        kind = QStringLiteral("DeckLink");
        what = QStringLiteral("%1 %2").arg(in.deckIndex + 1).arg(in.deckName.startsWith(QLatin1String("DeckLink ")) ? QString() : in.deckName);
    } else if (in.isNetwork() && !in.uri.trimmed().isEmpty()) {
        kind = in.type.toUpper();
        what = ch->sourceText().section(QLatin1Char(' '), 1);       // adres bez loginu i hasla
    }
    what = what.simplified();
    if (what.size() > 48) what = what.left(47).trimmed() + QStringLiteral("…");
    return what.isEmpty() ? QString() : kind + QStringLiteral(" - ") + what;
}

void Inspector::offerRename(Channel* ch, const QString& name) {
    const QString mode = AppSettings::renameOnSource();
    if (mode == QLatin1String("never")) return;
    if (mode == QLatin1String("always")) {
        m_autoNames.insert(ch, name);
        ch->setName(name);
        return;
    }
    auto* box = new QMessageBox(QMessageBox::Question, QStringLiteral("ISO Recorder"),
                                T("Rename the channel \"%1\" to \"%2\"?").arg(ch->config().name, name),
                                QMessageBox::Yes | QMessageBox::No, window());
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setDefaultButton(QMessageBox::Yes);
    auto* again = new QCheckBox(T("Don't ask again"));
    again->setToolTip(T("The answer will be used every time. You can change it in Settings."));
    box->setCheckBox(again);
    QPointer<Channel> guard(ch);
    connect(box, &QMessageBox::finished, this, [this, guard, name, again](int result) {
        const bool yes = result == QMessageBox::Yes;
        if (again->isChecked()) AppSettings::setRenameOnSource(yes ? QStringLiteral("always") : QStringLiteral("never"));
        if (yes && guard) {
            m_autoNames.insert(guard, name);
            guard->setName(name);
        }
    });
    box->open();
}

void Inspector::commitLive() {
    Channel* ch = m_mgr->active();
    if (!ch) return;
    LiveConfig live = ch->config().live;
    live.type = m_liveType->currentData().toString();
    live.ndiName = m_liveNdi->text().trimmed();
    live.srtAddress = m_liveSrt->text().trimmed();
    live.srtKbps = m_liveRate->currentData().toInt();
    live.when = m_liveWhen->currentData().toString();
    // domyslny port SRT wynika z numeru kanalu, zeby kanaly nie wchodzily sobie w droge
    if (live.type == QLatin1String("srt") && live.srtAddress.isEmpty())
        live.srtAddress = QStringLiteral(":%1").arg(9000 + m_mgr->indexOf(ch) + 1);
    ch->setLive(live);
}

void Inspector::bind() {
    Channel* ch = m_mgr->active();
    const int number = ch ? m_mgr->indexOf(ch) + 1 : 0;
    m_preview->setChannel(ch, number);
    m_meters->setChannel(ch);
    m_stack->setEnabled(ch != nullptr);
    disconnect(m_conn);
    if (ch) m_conn = connect(ch, &Channel::changed, this, &Inspector::refresh);
    refresh();
}

void Inspector::refresh() {
    Channel* ch = m_mgr->active();
    if (!ch) {
        m_title->setText(T("Inspector"));
        m_name->clear();
        m_lock->hide();
        refreshSchedule();
        return;
    }
    refreshSchedule();
    m_title->setText(QStringLiteral("%1&nbsp;&nbsp;<span style='color:#6e6e6e;font-weight:400'>%2</span>")
                         .arg(T("Inspector"), T("Channel %1 · %2")
                                                  .arg(m_mgr->indexOf(ch) + 1, 2, 10, QLatin1Char('0'))
                                                  .arg(ch->config().name.toHtmlEscaped())));
    if (!m_name->hasFocus()) m_name->setText(ch->config().name);
    // Input i Output sa zablokowane podczas nagrywania - poza glosnoscia wejscia, reakcja na zanik sygnalu i wyjsciem na zywo
    const bool editable = ch->canEdit();
    for (QWidget* w : {static_cast<QWidget*>(m_type), static_cast<QWidget*>(m_format)}) w->setEnabled(editable);
    m_outputForm->setEnabled(editable);
    m_lock->setVisible(!editable);
    const OutputConfig& out = ch->config().out;
    if (!m_outDir->hasFocus()) m_outDir->setText(QDir::toNativeSeparators(out.dir));
    m_outDir2Mode->setCurrentIndex(qMax(0, m_outDir2Mode->findData(out.dir2Mode)));
    if (!m_outDir2->hasFocus()) m_outDir2->setText(QDir::toNativeSeparators(out.dir2));
    const bool second = out.dir2Mode != QLatin1String("off");
    m_outDir2->setEnabled(second);
    m_outDir2Browse->setEnabled(second);
    if (m_outSpare->findData(out.spareMinMb) < 0) m_outSpare->addItem(T("below %1 GB free").arg(out.spareMinMb / 1024), out.spareMinMb);
    m_outSpare->setCurrentIndex(m_outSpare->findData(out.spareMinMb));
    m_outSpare->setEnabled(out.dir2Mode == QLatin1String("spare"));
    if (!m_outTemplate->hasFocus()) m_outTemplate->setText(out.nameTemplate);
    if (m_outCodec->findData(out.codec) < 0) m_outCodec->addItem(EncoderFactory::codecLabel(out.codec), out.codec);   // kodek z listy z innego komputera
    m_outCodec->setCurrentIndex(m_outCodec->findData(out.codec));
    if (m_outEncoder->property("forCodec").toString() != out.codec) fillEncoderCombo(m_outEncoder, out.codec);
    m_outEncoder->setCurrentIndex(qMax(0, m_outEncoder->findData(out.encoder)));
    m_outEncoder->setToolTip(m_outEncoder->currentText());
    m_outCodec->setEnabled(!ch->copyMode());
    enableContainersFor(m_outContainer, ch->copyMode() ? QString() : out.codec);
    // kopia strumienia: tylko SRT / RTSP; bitrate i dzwiek ustala wtedy zrodlo
    const bool canCopy = ch->config().in.type == QLatin1String("srt") || ch->config().in.type == QLatin1String("rtsp");
    if (auto* model = qobject_cast<QStandardItemModel*>(m_outEncoder->model()))
        if (QStandardItem* item = model->item(m_outEncoder->findData(QStringLiteral("copy")))) item->setEnabled(canCopy || out.encoder == QLatin1String("copy"));
    m_outVideoRate->setEnabled(!ch->copyMode() && !EncoderFactory::fixedBitrate(out.codec));
    m_outAudioRate->setEnabled(!ch->copyMode());
    m_outVideoRate->setToolTip(ch->copyMode() ? T("Set by the source (stream copy)")
                               : EncoderFactory::fixedBitrate(out.codec) ? T("ProRes 422 has a fixed quality: about 60 Mb/s for 720p and 120 Mb/s for 1080p")
                               : QString());
    m_outAudioRate->setToolTip(ch->copyMode() ? T("Set by the source (stream copy)") : QString());
    m_outVideoRate->setCurrentIndex(qMax(0, m_outVideoRate->findData(out.videoKbps)));
    m_outAudioRate->setCurrentIndex(qMax(0, m_outAudioRate->findData(out.audioKbps)));
    m_outContainer->setCurrentIndex(qMax(0, m_outContainer->findData(out.container)));
    m_outSplit->setCurrentIndex(qMax(0, m_outSplit->findData(out.splitSeconds > 0 ? qlonglong(out.splitSeconds) : -qlonglong(out.splitBytes))));
    const LiveConfig& live = ch->config().live;
    m_liveType->setCurrentIndex(qMax(0, m_liveType->findData(live.type)));
    if (!m_liveNdi->hasFocus()) m_liveNdi->setText(live.ndiName);
    m_liveNdi->setPlaceholderText(ch->config().name);
    if (!m_liveSrt->hasFocus()) m_liveSrt->setText(live.srtAddress);
    m_liveRate->setCurrentIndex(qMax(0, m_liveRate->findData(live.srtKbps)));
    m_liveNdi->setEnabled(live.type == QLatin1String("ndi"));
    m_liveSrt->setEnabled(live.type == QLatin1String("srt"));
    m_liveRate->setEnabled(live.type == QLatin1String("srt"));
    m_liveWhen->setCurrentIndex(qMax(0, m_liveWhen->findData(live.when)));
    m_liveWhen->setEnabled(live.type != QLatin1String("off"));
    m_pattern->setCurrentIndex(qMax(0, m_pattern->findData(ch->config().pattern)));
    const InputConfig& in = ch->config().in;
    m_type->setCurrentIndex(qMax(0, m_type->findData(in.type)));
    if (m_device->property("forType").toString() != in.type) fillDevices(in.type);
    int devIndex = -1;
    for (int i = 0; i < m_device->count(); ++i) {
        const QString id = m_device->itemData(i).toStringList().value(0);
        if ((in.type == QLatin1String("screen") && id.toInt() == in.screenIndex) || (in.type == QLatin1String("camera") && id == in.cameraPath) ||
            (in.type == QLatin1String("window") && id == in.windowTitle) || (in.type == QLatin1String("ndi") && id == in.uri) ||
            (in.type == QLatin1String("decklink") && id.toInt() == in.deckIndex)) devIndex = i;
    }
    if (in.type == QLatin1String("ndi") && devIndex < 0 && !in.uri.isEmpty()) {       // zapamietane zrodlo nie jest teraz widoczne w sieci
        const QSignalBlocker block(m_device);
        m_device->addItem(in.uri, QStringList{in.uri, in.uri});
        devIndex = m_device->count() - 1;
    }
    if (in.type == QLatin1String("window") && devIndex < 0 && !in.windowTitle.isEmpty()) {     // okno z listy zniknelo albo zmienilo tytul
        const QSignalBlocker block(m_device);
        m_device->addItem(in.windowTitle, QStringList{in.windowTitle, in.windowTitle});
        devIndex = m_device->count() - 1;
    }
    m_device->setCurrentIndex(devIndex);
    m_device->setEnabled(editable && (in.type == QLatin1String("screen") || in.type == QLatin1String("camera") || in.type == QLatin1String("window") ||
                                      in.type == QLatin1String("ndi") || in.type == QLatin1String("decklink")));
    if (in.type == QLatin1String("camera") && m_camMode->property("forCamera").toString() != in.cameraPath) fillCameraModes(in.cameraPath);
    if (in.type != QLatin1String("camera") && m_camMode->count() != 1) fillCameraModes(QString());
    int camIndex = m_camMode->findData(in.cameraMode);
    if (camIndex < 0 && !in.cameraMode.isEmpty())       // lista z wersji bez klatkazu: "1920x1080" -> pierwszy tryb o tym rozmiarze
        for (int i = 1; i < m_camMode->count() && camIndex < 0; ++i)
            if (m_camMode->itemData(i).toString().section(QLatin1Char('@'), 0, 0) == in.cameraMode) camIndex = i;
    m_camMode->setCurrentIndex(qMax(0, camIndex));
    m_camMode->setEnabled(editable && in.type == QLatin1String("camera"));
    m_loss->setCurrentIndex(qMax(0, m_loss->findData(in.lossPolicy)));
    m_loss->setEnabled(in.isNetwork() && !ch->copyMode());       // kopia strumienia: zanik zawsze konczy plik
    m_gain->setCurrentIndex(qMax(0, m_gain->findData(in.gainDb)));
    const bool hasAddress = in.isNetwork() && in.type != QLatin1String("ndi");
    if (!m_uri->hasFocus()) m_uri->setText(hasAddress ? in.uri : QString());
    m_uri->setEnabled(editable && hasAddress);
    m_uri->setPlaceholderText(in.type == QLatin1String("srt") ? QStringLiteral("host:9000   |   :9000")
                              : in.type == QLatin1String("whep") ? QStringLiteral("http://host:8889/stream/whep")
                              : in.type == QLatin1String("rtsp") ? QStringLiteral("rtsp://host:554/stream") : QString());
    m_uri->setToolTip(in.type == QLatin1String("srt")
        ? T("host:9000 — this computer connects to the sender;  :9000 — this computer waits for the sender on port 9000")
        : in.type == QLatin1String("rtsp") ? T("rtsp://host:554/stream — use rtspt:// to force TCP")
        : in.type == QLatin1String("whep") ? T("The WHEP address of the stream on the server, for example http://host:8889/stream/whep") : QString());
    const bool off = in.type == QLatin1String("none");      // wejscie wylaczone: nie ma czego ustawiac
    m_wave->setEnabled(editable && !in.embeddedAudio() && !off);      // dzwiek pochodzi ze strumienia albo z karty
    m_gain->setEnabled(!off);
    m_format->setEnabled(editable && !off);
    m_fps->setEnabled(editable && !off);
    if (m_fps->findData(ch->config().fps) < 0) m_fps->addItem(T("%1 fps").arg(ch->config().fps), ch->config().fps);     // wartosc z pliku
    m_fps->setCurrentIndex(m_fps->findData(ch->config().fps));
    m_reconnect->setEnabled(editable && !off);
    m_pattern->setEnabled(editable && in.type == QLatin1String("test"));
    int auIndex = 0;
    for (int i = 0; i < m_wave->count(); ++i) {
        const QStringList d = m_wave->itemData(i).toStringList();
        if (d.value(0) != in.audioType) continue;
        if (in.audioType == QLatin1String("test") ? d.value(3).toInt() == ch->config().audioWave : d.value(1) == in.audioDevice) { auIndex = i; break; }
    }
    m_wave->setCurrentIndex(auIndex);
    m_format->setCurrentIndex(qMax(0, m_format->findData(QSize(ch->config().width, ch->config().height))));
}
