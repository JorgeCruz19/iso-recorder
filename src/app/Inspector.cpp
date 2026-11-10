#include "Inspector.h"

#include <QButtonGroup>
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
    g->setColumnMinimumWidth(0, 96);
    g->setColumnStretch(1, 1);
    into->addLayout(g);
    return g;
}

void row(QGridLayout* g, const QString& label, QWidget* field) {
    const int r = g->rowCount();
    auto* l = new QLabel(label);
    l->setObjectName(QStringLiteral("fl"));
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
    m_type->addItem(T("Test generator"));
    const struct { QString name; const char* from; } types[] = {
        {QStringLiteral("HDMI — Blackmagic DeckLink"), Roadmap::ndiDecklink},
        {QStringLiteral("HDMI — UVC / Magewell"), Roadmap::localInputs},
        {T("Screen grab"), Roadmap::localInputs},
        {QStringLiteral("SRT"), Roadmap::network},
        {QStringLiteral("RTSP"), Roadmap::network},
        {QStringLiteral("NDI"), Roadmap::ndiDecklink},
        {QStringLiteral("WebRTC WHEP"), Roadmap::webrtc},
        {QStringLiteral("WebRTC WHIP"), Roadmap::webrtc}};
    for (const auto& t : types) m_type->addItem(t.name + QStringLiteral("  ") + T("(from %1)").arg(QLatin1String(t.from)));
    if (auto* model = qobject_cast<QStandardItemModel*>(m_type->model()))
        for (int i = 1; i < model->rowCount(); ++i) model->item(i)->setEnabled(false);
    row(g, T("Input type"), m_type);
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
    m_wave = new QComboBox;
    m_wave->addItem(T("Sine 440 Hz"), 0);
    m_wave->addItem(T("Ticks every 1 s"), 8);
    m_wave->addItem(T("Silence"), 4);
    row(g, T("Audio signal"), m_wave);

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
    connect(m_wave, &QComboBox::activated, this, [this](int i) {
        if (Channel* ch = m_mgr->active()) ch->setAudioWave(m_wave->itemData(i).toInt());
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
    // Input i Output sa zablokowane podczas nagrywania
    const bool editable = ch->canEdit();
    m_inputPage->setEnabled(editable);
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
    m_wave->setCurrentIndex(qMax(0, m_wave->findData(ch->config().audioWave)));
    m_format->setCurrentIndex(qMax(0, m_format->findData(QSize(ch->config().width, ch->config().height))));
}
