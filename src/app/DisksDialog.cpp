#include "DisksDialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/ChannelManager.h"
#include "core/DiskMonitor.h"
#include "core/DiskTest.h"
#include "core/Lang.h"

namespace {
QString gb(qint64 bytes) {
    const double g = bytes / 1073741824.0;
    return g >= 1000 ? QStringLiteral("%1 TB").arg(QLocale().toString(g / 1024.0, 'f', 2))
                     : QStringLiteral("%1 GB").arg(QLocale().toString(g, 'f', 0));
}
enum Col { Disk, Free, Channels, Needed, Left, Test, Buttons, ColCount };
}

QString DisksDialog::resultText(const DiskTestResult& r, double requiredMbps) {
    if (!r.when.isValid()) return T("Not tested");
    if (!r.ok) return r.error;
    QString text = T("%1 MB/s average, %2 MB/s worst second").arg(qRound(r.avgMBs)).arg(qRound(r.worstMBs));
    switch (DiskMonitor::verdict(r, requiredMbps)) {
    case DiskMonitor::Verdict::Good: text += QStringLiteral(" — ") + T("OK"); break;
    case DiskMonitor::Verdict::Marginal: text += QStringLiteral(" — ") + T("marginal"); break;
    case DiskMonitor::Verdict::TooSlow: text += QStringLiteral(" — ") + T("too slow"); break;
    case DiskMonitor::Verdict::Unknown: break;
    }
    return text;
}

DisksDialog::DisksDialog(ChannelManager* mgr, QWidget* parent) : QDialog(parent), m_mgr(mgr) {
    setWindowTitle(T("Disks"));
    resize(900, 300);
    auto* v = new QVBoxLayout(this);
    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({T("Disk"), T("Free"), T("Channels"), T("Needed"), T("Time left"), T("Write test"), QString()});
    m_table->verticalHeader()->hide();
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(Test, QHeaderView::Stretch);
    v->addWidget(m_table, 1);
    m_status = new QLabel(T("The test writes temporary files (up to 8 GB) to the recording folder and removes them."));
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#9d9d9d;"));
    v->addWidget(m_status);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &DisksDialog::refresh);
    timer->start(1000);
    refresh();
}

DisksDialog::~DisksDialog() {
    if (m_test) {
        m_test->cancel();
        m_test->wait();
    }
}

void DisksDialog::refresh() {
    const QList<VolumeInfo> vols = DiskMonitor::volumes(m_mgr->channels());
    QStringList roots;
    for (const VolumeInfo& v : vols) roots << v.root;
    if (roots != m_roots) {             // zmienil sie zestaw wolumenow - zbuduj wiersze i przyciski od nowa
        m_roots = roots;
        m_table->setRowCount(int(roots.size()));
        for (int row = 0; row < roots.size(); ++row) {
            for (int c = 0; c < Buttons; ++c) m_table->setItem(row, c, new QTableWidgetItem);
            auto* cell = new QWidget;
            auto* h = new QHBoxLayout(cell);
            h->setContentsMargins(6, 2, 6, 2);
            h->setSpacing(6);
            const QString root = roots.at(row);
            for (int seconds : {10, 60}) {
                auto* b = new QPushButton(T("Test %1 s").arg(seconds));
                b->setObjectName(QStringLiteral("dbtn"));
                b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 34);
                connect(b, &QPushButton::clicked, this, [this, root, seconds] { startTest(root, seconds); });
                h->addWidget(b);
            }
            m_table->setCellWidget(row, Buttons, cell);
        }
    }
    for (int row = 0; row < vols.size(); ++row) {
        const VolumeInfo& v = vols.at(row);
        m_table->item(row, Disk)->setText(v.name() + QStringLiteral("  ") + v.fileSystem);
        m_table->item(row, Free)->setText(T("%1 free of %2").arg(gb(v.free), gb(v.total)));
        m_table->item(row, Channels)->setText(v.recording > 0 ? QStringLiteral("%1  (REC %2)").arg(v.channels).arg(v.recording)
                                                               : QString::number(v.channels));
        m_table->item(row, Needed)->setText(QStringLiteral("%1 Mb/s").arg(QLocale().toString(v.requiredMbps(), 'f', 1)));
        m_table->item(row, Left)->setText(DiskMonitor::formatLeft(v.secondsLeft));
        const bool running = m_test && m_testRoot == v.root;
        m_table->item(row, Test)->setText(running ? m_testProgress : resultText(DiskMonitor::lastTest(v.root), v.requiredMbps()));
        if (QWidget* cell = m_table->cellWidget(row, Buttons)) cell->setEnabled(!m_test && v.recording == 0);
    }
}

void DisksDialog::startTest(const QString& root, int seconds) {
    if (m_test) return;
    // Folder i liczba strumieni: tak, jak beda pisac kanaly przypisane do tego wolumenu.
    QString dir;
    int streams = 0;
    for (const Channel* c : m_mgr->channels()) {
        if (DiskMonitor::rootFor(c->outputDir()) != root) continue;
        if (c->isRecording()) {
            m_status->setText(T("Cannot test while recording on this disk"));
            return;
        }
        if (dir.isEmpty()) dir = c->outputDir();
        ++streams;
    }
    if (dir.isEmpty()) dir = Channel::defaultOutputDir();
    m_testRoot = root;
    m_testProgress = T("Testing… %1 / %2 s").arg(0).arg(seconds);
    m_test = new DiskTest(dir, seconds, qMax(1, streams), this);
    connect(m_test, &DiskTest::progress, this, [this](int s, int of, double mbs) {
        m_testProgress = T("Testing… %1 / %2 s").arg(s).arg(of) + QStringLiteral("  ·  %1 MB/s").arg(qRound(mbs));
        refresh();
    });
    connect(m_test, &DiskTest::done, this, [this, root](const DiskTestResult& r) {
        DiskMonitor::setLastTest(root, r);
        m_test = nullptr;           // obiekt watku usuwa sie sam po zakonczeniu (finished -> deleteLater)
        refresh();
    });
    connect(m_test, &QThread::finished, m_test, &QObject::deleteLater);
    m_test->start();
    refresh();
}
