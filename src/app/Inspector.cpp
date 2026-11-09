#include "Inspector.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "Widgets.h"
#include "core/ChannelManager.h"

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
    g->setColumnMinimumWidth(0, 150);
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
    m_stack->addWidget(placeholder(QStringLiteral(
        "Folder, szablon nazwy, kontener, kodeki i auto-split pojawią się w wersji 0.2 razem z nagrywaniem.")));
    m_stack->addWidget(placeholder(QStringLiteral("Harmonogram startu, stopu i splitu pojawi się w wersji 0.9.")));
    v->addWidget(m_stack, 1);

    connect(mgr, &ChannelManager::activeChanged, this, &Inspector::bind);
    connect(mgr, &ChannelManager::listChanged, this, &Inspector::bind);
    bind();
}

QWidget* Inspector::buildInputPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    v->addWidget(section(QStringLiteral("Źródło")));
    QGridLayout* g = form(v);
    m_name = new QLineEdit;
    row(g, QStringLiteral("Nazwa kanału"), m_name);
    m_type = new QComboBox;
    m_type->addItem(QStringLiteral("Generator testowy"));
    for (const char* t : {"HDMI — Blackmagic DeckLink (od 0.8)", "HDMI — UVC / Magewell (od 0.6)", "Screen grab (od 0.6)",
                          "SRT (od 0.7)", "RTSP (od 0.7)", "NDI (od 0.8)", "WebRTC WHEP (od 1.0)", "WebRTC WHIP (od 1.0)"})
        m_type->addItem(QString::fromUtf8(t));
    if (auto* model = qobject_cast<QStandardItemModel*>(m_type->model()))
        for (int i = 1; i < model->rowCount(); ++i) model->item(i)->setEnabled(false);
    row(g, QStringLiteral("Typ wejścia"), m_type);
    m_pattern = new QComboBox;
    const struct { const char* name; int id; } patterns[] = {
        {"Pasy SMPTE", 0}, {"Pasy SMPTE 75%", 13}, {"Pasy SMPTE 100%", 19}, {"Piłka", 18}, {"Wiatraczek", 21},
        {"Szprychy", 22}, {"Okręgi", 11}, {"Gradient", 23}, {"Śnieg", 1}, {"Czarny", 2}};
    for (const auto& pt : patterns) m_pattern->addItem(QString::fromUtf8(pt.name), pt.id);
    row(g, QStringLiteral("Wzór"), m_pattern);
    m_format = new QLabel;
    row(g, QStringLiteral("Format wideo"), m_format);

    v->addWidget(section(QStringLiteral("Audio")));
    g = form(v);
    m_wave = new QComboBox;
    m_wave->addItem(QStringLiteral("Sinus 440 Hz"), 0);
    m_wave->addItem(QStringLiteral("Tiki co 1 s"), 8);
    m_wave->addItem(QStringLiteral("Cisza"), 4);
    row(g, QStringLiteral("Sygnał audio"), m_wave);

    v->addWidget(section(QStringLiteral("Output — od wersji 0.2"), true));
    v->addWidget(section(QStringLiteral("Scheduler — od wersji 0.9"), true));
    v->addStretch(1);

    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        if (Channel* ch = m_mgr->active()) ch->setName(m_name->text());
    });
    connect(m_pattern, &QComboBox::activated, this, [this](int i) {
        if (Channel* ch = m_mgr->active()) ch->setPattern(m_pattern->itemData(i).toInt());
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
        m_title->setText(QStringLiteral("Inspektor"));
        m_name->clear();
        m_format->clear();
        return;
    }
    m_title->setText(QStringLiteral("Inspektor&nbsp;&nbsp;<span style='color:#6e6e6e;font-weight:400'>Kanał %1 · %2</span>")
                         .arg(m_mgr->indexOf(ch) + 1, 2, 10, QLatin1Char('0')).arg(ch->config().name.toHtmlEscaped()));
    if (!m_name->hasFocus()) m_name->setText(ch->config().name);
    m_pattern->setCurrentIndex(qMax(0, m_pattern->findData(ch->config().pattern)));
    m_wave->setCurrentIndex(qMax(0, m_wave->findData(ch->config().audioWave)));
    m_format->setText(QStringLiteral("%1 × %2, %3 kl./s").arg(ch->config().width).arg(ch->config().height).arg(ch->config().fps));
}
