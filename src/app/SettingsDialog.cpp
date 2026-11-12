#include "SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "core/AppSettings.h"
#include "core/Lang.h"
#include "media/Devices.h"
#include "media/EncoderFactory.h"
#include "util/FileNameTemplate.h"

QString nameTemplateHelp() {
    return T("Variables for the name:\n"
             "{session} — session name\n"
             "{channel} — channel name\n"
             "{index} — channel number; {index:02} pads with zeros\n"
             "{source} — source description\n"
             "{date} — recording start date (YYYY-MM-DD)\n"
             "{time} — recording start time (HH-MM-SS)\n"
             "{part} — file part number; {part:03} pads with zeros");
}

namespace {
QLabel* heading(const QString& text) {
    auto* l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("font-weight:700; color:#f0f0f0; padding-top:10px;"));
    return l;
}

QLabel* note(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#9d9d9d; font-size:11px;"));
    return l;
}
}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(T("Settings"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setMinimumWidth(620);
    setStyleSheet(QStringLiteral(
        "QLineEdit, QComboBox { background:#1a1a1a; border:1px solid #3a3a3a; border-radius:2px; padding:2px 8px; min-height:22px; }"
        "QComboBox QAbstractItemView { background:#1a1a1a; border:1px solid #3a3a3a; selection-background-color:#4a4a4a; }"));
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(18, 10, 18, 14);
    v->setSpacing(6);

    // ---- domyslne parametry nagrywania
    v->addWidget(heading(T("Default recording settings")));
    v->addWidget(note(T("Every newly added channel starts with these settings. Existing channels are not changed.")));
    auto* g = new QGridLayout;
    g->setColumnStretch(1, 1);
    g->setHorizontalSpacing(12);
    g->setVerticalSpacing(7);
    int r = 0;
    auto row = [&](const QString& label, QWidget* field, const QString& tip = {}) {
        auto* l = new QLabel(label);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        l->setStyleSheet(QStringLiteral("color:#9d9d9d;"));
        if (!tip.isEmpty()) {
            l->setToolTip(tip);
            field->setToolTip(tip);
        }
        g->addWidget(l, r, 0);
        g->addWidget(field, r++, 1);
    };
    const OutputConfig def = AppSettings::defaultOutput();

    auto* dirRow = new QWidget;
    auto* dh = new QHBoxLayout(dirRow);
    dh->setContentsMargins(0, 0, 0, 0);
    dh->setSpacing(4);
    m_dir = new QLineEdit(QDir::toNativeSeparators(def.dir));
    m_dir->setPlaceholderText(QDir::toNativeSeparators(Channel::defaultOutputDir()));
    auto* browse = new QPushButton(QStringLiteral("..."));
    browse->setFixedWidth(40);
    dh->addWidget(m_dir, 1);
    dh->addWidget(browse);
    row(T("Folder"), dirRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString start = m_dir->text().trimmed().isEmpty() ? Channel::defaultOutputDir() : m_dir->text().trimmed();
        const QString dir = QFileDialog::getExistingDirectory(this, T("Choose recording folder"), start);
        if (!dir.isEmpty()) m_dir->setText(QDir::toNativeSeparators(dir));
    });

    m_template = new QLineEdit(def.nameTemplate);
    m_template->setPlaceholderText(QLatin1String(FileNameTemplate::kDefault));
    row(T("Name template"), m_template, nameTemplateHelp());

    m_container = new QComboBox;
    m_container->addItem(QStringLiteral("MKV (Matroska)"), QStringLiteral("mkv"));
    m_container->addItem(QStringLiteral("MPEG-TS"), QStringLiteral("ts"));
    m_container->setCurrentIndex(qMax(0, m_container->findData(def.container)));
    row(T("Container"), m_container);

    m_split = new QComboBox;
    m_split->addItem(T("Off"), qlonglong(0));
    for (int min : {1, 5, 10, 15, 30, 60}) m_split->addItem(T("every %1 min").arg(min), qlonglong(min * 60));
    for (int gbs : {1, 2, 4}) m_split->addItem(T("every %1 GB").arg(gbs), qlonglong(-(qlonglong(gbs) << 30)));
    m_split->setCurrentIndex(qMax(0, m_split->findData(def.splitSeconds > 0 ? qlonglong(def.splitSeconds) : -qlonglong(def.splitBytes))));
    row(T("Auto-split"), m_split);

    m_encoder = new QComboBox;
    m_encoder->addItem(T("Auto (hardware first)"), QStringLiteral("auto"));
    for (const QString& e : EncoderFactory::installed()) m_encoder->addItem(QStringLiteral("H.264 — ") + EncoderFactory::displayName(e), e);
    // zapis bez ponownego kodowania: pozycja jest widoczna, ale jeszcze niedostepna (patrz opis pod spodem)
    m_encoder->addItem(T("No re-encoding (stream copy)") + QStringLiteral("  ") + T("(from %1)").arg(QLatin1String(Roadmap::streamCopy)), QStringLiteral("copy"));
    if (auto* model = qobject_cast<QStandardItemModel*>(m_encoder->model())) model->item(m_encoder->count() - 1)->setEnabled(false);
    m_encoder->setCurrentIndex(qMax(0, m_encoder->findData(def.encoder)));
    row(T("Encoder"), m_encoder);

    m_videoRate = new QComboBox;
    for (int mb : {2, 4, 6, 8, 12, 20, 35, 50}) m_videoRate->addItem(QStringLiteral("%1 Mb/s").arg(mb), mb * 1000);
    m_videoRate->setCurrentIndex(qMax(0, m_videoRate->findData(def.videoKbps)));
    row(T("Video bitrate"), m_videoRate);

    m_audioRate = new QComboBox;
    for (int kb : {128, 192, 256, 320}) m_audioRate->addItem(QStringLiteral("AAC  %1 kb/s").arg(kb), kb);
    m_audioRate->setCurrentIndex(qMax(0, m_audioRate->findData(def.audioKbps)));
    row(T("Audio bitrate"), m_audioRate);
    v->addLayout(g);
    v->addWidget(note(T("No re-encoding (stream copy): the program will write the SRT / RTSP stream exactly as it arrives — the same "
                        "codec, picture size, bitrate and quality as at the sender, with almost no processor load. What you get: a file "
                        "identical in quality to the stream. What you lose: the bitrate, picture size and codec cannot be changed, the file "
                        "can only be cut at the sender's key frames (a split may come up to a few seconds late), a pause starts a new file "
                        "part, and during a signal loss nothing is written (there is no black picture to fill the gap). It works only for "
                        "network sources, not for a screen, a window or a camera.")));

    // ---- odsluch
    v->addWidget(heading(T("Monitoring")));
    v->addWidget(note(T("The sound card on which a channel is played when you click its headphones icon. Only one channel plays at a time.")));
    g = new QGridLayout;
    g->setColumnStretch(1, 1);
    g->setHorizontalSpacing(12);
    r = 0;
    m_monitor = new QComboBox;
    m_monitor->addItem(T("Default system output"), QString());
    for (const Devices::Info& d : Devices::list(true).audioOutputs) m_monitor->addItem(d.name, d.id);
    m_monitor->setCurrentIndex(qMax(0, m_monitor->findData(AppSettings::monitorDevice())));
    row(T("Sound card"), m_monitor);
    v->addLayout(g);

    // ---- split do nowego folderu
    v->addWidget(heading(T("Split to new folder")));
    v->addWidget(note(T("The folder is created inside the channel's recording folder. The same variables as in the file name, without {part}.")));
    g = new QGridLayout;
    g->setColumnStretch(1, 1);
    g->setHorizontalSpacing(12);
    r = 0;
    m_folderTemplate = new QLineEdit(AppSettings::splitFolderTemplate());
    m_folderTemplate->setPlaceholderText(QLatin1String(AppSettings::kSplitFolderDefault));
    row(T("Folder name"), m_folderTemplate, nameTemplateHelp());
    v->addLayout(g);

    // ---- nazwa kanalu wg zrodla
    v->addWidget(heading(T("Channel names")));
    v->addWidget(note(T("A channel that still has its default name (TEST 1, TEST 2 …) can take the name of its source, for example "
                        "\"NDI - STUDIO (Cam 1)\". \"Ask\" also brings the question back after \"Don't ask again\".")));
    g = new QGridLayout;
    g->setColumnStretch(1, 1);
    g->setHorizontalSpacing(12);
    r = 0;
    m_rename = new QComboBox;
    m_rename->addItem(T("Ask"), QStringLiteral("ask"));
    m_rename->addItem(T("Always rename"), QStringLiteral("always"));
    m_rename->addItem(T("Never rename"), QStringLiteral("never"));
    m_rename->setCurrentIndex(qMax(0, m_rename->findData(AppSettings::renameOnSource())));
    row(T("After a source change"), m_rename);
    v->addLayout(g);

    v->addSpacing(8);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(box);
}

void SettingsDialog::save() {
    OutputConfig o;
    o.dir = QDir::fromNativeSeparators(m_dir->text().trimmed());
    o.nameTemplate = m_template->text().trimmed();
    o.container = m_container->currentData().toString();
    const qlonglong split = m_split->currentData().toLongLong();
    o.splitSeconds = split > 0 ? int(split) : 0;
    o.splitBytes = split < 0 ? -split : 0;
    o.encoder = m_encoder->currentData().toString();
    o.videoKbps = m_videoRate->currentData().toInt();
    o.audioKbps = m_audioRate->currentData().toInt();
    AppSettings::setDefaultOutput(o);
    AppSettings::setMonitorDevice(m_monitor->currentData().toString());
    AppSettings::setSplitFolderTemplate(m_folderTemplate->text());
    AppSettings::setRenameOnSource(m_rename->currentData().toString());
}
