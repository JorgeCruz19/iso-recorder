#include "Inspector.h"

#include <QButtonGroup>
#include <QComboBox>
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
#include "core/Lang.h"

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

    m_stack->addWidget(buildInputPage());
    m_stack->addWidget(placeholder(
        T("Folder, name template, container, codecs and auto-split will arrive in version %1 together with recording.")
            .arg(QLatin1String(Roadmap::recording))));
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

    v->addWidget(section(T("Output — from version %1").arg(QLatin1String(Roadmap::recording)), true));
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
        return;
    }
    m_title->setText(QStringLiteral("%1&nbsp;&nbsp;<span style='color:#6e6e6e;font-weight:400'>%2</span>")
                         .arg(T("Inspector"), T("Channel %1 · %2")
                                                  .arg(m_mgr->indexOf(ch) + 1, 2, 10, QLatin1Char('0'))
                                                  .arg(ch->config().name.toHtmlEscaped())));
    if (!m_name->hasFocus()) m_name->setText(ch->config().name);
    m_pattern->setCurrentIndex(qMax(0, m_pattern->findData(ch->config().pattern)));
    m_wave->setCurrentIndex(qMax(0, m_wave->findData(ch->config().audioWave)));
    m_format->setCurrentIndex(qMax(0, m_format->findData(QSize(ch->config().width, ch->config().height))));
}
