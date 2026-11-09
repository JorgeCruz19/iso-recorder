#pragma once
#include <QStringList>
#include <QWidget>

class ChannelGrid;
class ChannelManager;
class Inspector;
class KeyButton;
class QLabel;
class QProgressBar;
class QPushButton;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(ChannelManager* mgr, QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QWidget* buildHeader();
    QWidget* buildControlPanel();
    QWidget* buildStatus();
    void refreshCounts();
    void refreshActive();
    void tickSecond();
    void notice(const QString& text);
    void showDiagnostics();

    ChannelManager* m_mgr;
    ChannelGrid* m_grid;
    Inspector* m_inspector;

    QLabel* m_session;
    QLabel* m_recCap;
    QLabel* m_recTime;
    QLabel* m_recBadge;
    QLabel* m_rtc;
    QLabel* m_diskCap;
    QLabel* m_diskTime;
    QLabel* m_diskFree;
    QProgressBar* m_diskBar;
    QLabel* m_cpu;

    QLabel* m_selCap;
    KeyButton* m_btnTransport[4]{};
    KeyButton* m_btnSelectAll;
    KeyButton* m_btnAdd;
    KeyButton* m_btnUp;
    KeyButton* m_btnDown;
    QLabel* m_count;
    QLabel* m_legend;

    QLabel* m_events;
    QLabel* m_hint;
    QStringList m_log;
    quint64 m_cpuIdle = 0, m_cpuTotal = 0;
};
