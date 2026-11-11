#include "Inspector.h"

#include <QButtonGroup>
#include <QAbstractItemView>
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

#include "Widgets.h"
#include "core/ChannelManager.h"
#include "core/DiskMonitor.h"
#include "core/Lang.h"
#include <QStorageInfo>
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

void row(QGridLayout* g, const QString& label, QWidget* field) {
    const int r = g->rowCount();
    auto* l = new QLabel(label);
    l->setObjectName(QStringLiteral("fl"));
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setWordWrap(true);                   // dluga etykieta (tlumaczenia) lamie sie na dwie linie zamiast byc obcieta
    l->setFixedWidth(104);
    g->addWidget(l, r, 0);
    g->addWidget(field, r, 1);
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
    m_stack->addWidget(placeholder(T("Start, stop and split scheduling will arrive in version %1.").arg(QLatin1String(Roadmap::scheduler))));
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
    for (QLineEdit* e : findChildren<QLineEdit*>()) e->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
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
    m_type->addItem(T("Test generator"), QStringLiteral("test"));
    m_type->addItem(T("Screen grab"), QStringLiteral("screen"));
    m_type->addItem(T("Window capture"), QStringLiteral("window"));
    m_type->addItem(T("Camera (UVC, capture card)"), QStringLiteral("camera"));
    m_type->addItem(QStringLiteral("SRT"), QStringLiteral("srt"));
    m_type->addItem(QStringLiteral("RTSP"), QStringLiteral("rtsp"));
    const int firstFuture = m_type->count();
    const struct { QString name; const char* from; } types[] = {
        {QStringLiteral("HDMI — Blackmagic DeckLink"), Roadmap::ndiDecklink},
        {QStringLiteral("NDI"), Roadmap::ndiDecklink},
        {QStringLiteral("WebRTC WHEP"), Roadmap::webrtc},
        {QStringLiteral("WebRTC WHIP"), Roadmap::webrtc}};
    for (const auto& t : types) m_type->addItem(t.name + QStringLiteral("  ") + T("(from %1)").arg(QLatin1String(t.from)));
    if (auto* model = qobject_cast<QStandardItemModel*>(m_type->model()))
        for (int i = firstFuture; i < model->rowCount(); ++i) model->item(i)->setEnabled(false);
    row(g, T("Input type"), m_type);
    m_device = new QComboBox;
    row(g, T("Device"), m_device);
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
    row(g, T("Pattern"), m_pattern);
    m_format = new QComboBox;
    const struct { int w, h; const char* ratio; } formats[] = {
        {1280, 720, "16:9"}, {1920, 1080, "16:9"}, {1024, 768, "4:3"}, {720, 1280, "9:16"}, {1920, 800, "2.40:1"}, {1080, 1080, "1:1"}};
    for (const auto& f : formats)
        m_format->addItem(QStringLiteral("%1 × %2  ·  %3").arg(f.w).arg(f.h).arg(QLatin1String(f.ratio)), QSize(f.w, f.h));
    row(g, T("Video format"), m_format);

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

    v->addWidget(section(T("Scheduler — from version %1").arg(QLatin1String(Roadmap::scheduler)), true));
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
    m_outTemplate = new QLineEdit;
    m_outTemplate->setPlaceholderText(QLatin1String(FileNameTemplate::kDefault));
    m_outTemplate->setToolTip(QStringLiteral("{session} {channel} {source} {index:02} {date} {time} {part:03}"));
    row(g, T("Name template"), m_outTemplate);
    m_outContainer = new QComboBox;
    m_outContainer->addItem(QStringLiteral("MKV (Matroska)"), QStringLiteral("mkv"));
    m_outContainer->addItem(QStringLiteral("MPEG-TS"), QStringLiteral("ts"));
    row(g, T("Container"), m_outContainer);
    // Auto-split: dane = sekundy (dodatnie) albo bajty zapisane jako liczba ujemna
    m_outSplit = new QComboBox;
    m_outSplit->addItem(T("Off"), qlonglong(0));
    for (int min : {1, 5, 10, 15, 30, 60}) m_outSplit->addItem(T("every %1 min").arg(min), qlonglong(min * 60));
    for (int gbs : {1, 2, 4}) m_outSplit->addItem(T("every %1 GB").arg(gbs), qlonglong(-(qlonglong(gbs) << 30)));
    row(g, T("Auto-split"), m_outSplit);

    fv->addWidget(section(T("Video")));
    g = form(fv);
    m_outEncoder = new QComboBox;
    m_outEncoder->addItem(T("Auto (hardware first)"), QStringLiteral("auto"));
    for (const QString& e : EncoderFactory::installed())
        m_outEncoder->addItem(QStringLiteral("H.264 — ") + EncoderFactory::displayName(e), e);
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
    connect(m_liveType, &QComboBox::activated, this, &Inspector::commitLive);
    connect(m_liveRate, &QComboBox::activated, this, &Inspector::commitLive);
    connect(m_liveNdi, &QLineEdit::editingFinished, this, &Inspector::commitLive);
    connect(m_liveSrt, &QLineEdit::editingFinished, this, &Inspector::commitLive);
    v->addStretch(1);

    connect(m_outDir, &QLineEdit::editingFinished, this, &Inspector::commitOutput);
    connect(m_outTemplate, &QLineEdit::editingFinished, this, &Inspector::commitOutput);
    for (QComboBox* c : {m_outEncoder, m_outVideoRate, m_outAudioRate, m_outContainer, m_outSplit})
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

void Inspector::commitOutput() {
    Channel* ch = m_mgr->active();
    if (!ch || !ch->canEdit()) return;
    OutputConfig out = ch->config().out;
    out.dir = QDir::fromNativeSeparators(m_outDir->text().trimmed());
    out.nameTemplate = m_outTemplate->text().trimmed();
    out.encoder = m_outEncoder->currentData().toString();
    out.videoKbps = m_outVideoRate->currentData().toInt();
    out.audioKbps = m_outAudioRate->currentData().toInt();
    out.container = m_outContainer->currentData().toString();
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
            for (const QString& m : d.modes) m_camMode->addItem(QString(m).replace(QLatin1Char('x'), QStringLiteral(" × ")), m);
    m_camMode->setProperty("forCamera", cameraPath);
}

void Inspector::commitInput() {
    Channel* ch = m_mgr->active();
    if (!ch) return;
    InputConfig in = ch->config().in;
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
    in.uri = m_uri->text().trimmed();
    const QStringList au = m_wave->currentData().toStringList();
    in.audioType = au.value(0, QStringLiteral("test"));
    in.audioDevice = au.value(1);
    in.audioName = au.value(2);
    if (in.audioType == QLatin1String("test")) ch->setAudioWave(au.value(3).toInt());
    ch->setInput(in);
}

void Inspector::commitLive() {
    Channel* ch = m_mgr->active();
    if (!ch) return;
    LiveConfig live = ch->config().live;
    live.type = m_liveType->currentData().toString();
    live.ndiName = m_liveNdi->text().trimmed();
    live.srtAddress = m_liveSrt->text().trimmed();
    live.srtKbps = m_liveRate->currentData().toInt();
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
        return;
    }
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
    if (!m_outTemplate->hasFocus()) m_outTemplate->setText(out.nameTemplate);
    m_outEncoder->setCurrentIndex(qMax(0, m_outEncoder->findData(out.encoder)));
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
    m_pattern->setCurrentIndex(qMax(0, m_pattern->findData(ch->config().pattern)));
    const InputConfig& in = ch->config().in;
    m_type->setCurrentIndex(qMax(0, m_type->findData(in.type)));
    if (m_device->property("forType").toString() != in.type) fillDevices(in.type);
    int devIndex = -1;
    for (int i = 0; i < m_device->count(); ++i) {
        const QString id = m_device->itemData(i).toStringList().value(0);
        if ((in.type == QLatin1String("screen") && id.toInt() == in.screenIndex) || (in.type == QLatin1String("camera") && id == in.cameraPath) ||
            (in.type == QLatin1String("window") && id == in.windowTitle)) devIndex = i;
    }
    if (in.type == QLatin1String("window") && devIndex < 0 && !in.windowTitle.isEmpty()) {     // okno z listy zniknelo albo zmienilo tytul
        const QSignalBlocker block(m_device);
        m_device->addItem(in.windowTitle, QStringList{in.windowTitle, in.windowTitle});
        devIndex = m_device->count() - 1;
    }
    m_device->setCurrentIndex(devIndex);
    m_device->setEnabled(editable && (in.type == QLatin1String("screen") || in.type == QLatin1String("camera") || in.type == QLatin1String("window")));
    if (in.type == QLatin1String("camera") && m_camMode->property("forCamera").toString() != in.cameraPath) fillCameraModes(in.cameraPath);
    if (in.type != QLatin1String("camera") && m_camMode->count() != 1) fillCameraModes(QString());
    m_camMode->setCurrentIndex(qMax(0, m_camMode->findData(in.cameraMode)));
    m_camMode->setEnabled(editable && in.type == QLatin1String("camera"));
    m_loss->setCurrentIndex(qMax(0, m_loss->findData(in.lossPolicy)));
    m_loss->setEnabled(in.isNetwork());
    m_gain->setCurrentIndex(qMax(0, m_gain->findData(in.gainDb)));
    if (!m_uri->hasFocus()) m_uri->setText(in.uri);
    m_uri->setEnabled(editable && in.isNetwork());
    m_uri->setPlaceholderText(in.type == QLatin1String("srt") ? QStringLiteral("host:9000   |   :9000")
                              : in.type == QLatin1String("rtsp") ? QStringLiteral("rtsp://host:554/stream") : QString());
    m_uri->setToolTip(in.type == QLatin1String("srt")
        ? T("host:9000 — this computer connects to the sender;  :9000 — this computer waits for the sender on port 9000")
        : in.type == QLatin1String("rtsp") ? T("rtsp://host:554/stream — use rtspt:// to force TCP") : QString());
    m_wave->setEnabled(editable && !in.isNetwork());      // dzwiek pochodzi ze strumienia
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
