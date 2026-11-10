#pragma once
#include <QStringList>
#include <QWidget>

class ChannelGrid;
class ChannelManager;
enum class Transport;
class Inspector;
class KeyButton;
class QLabel;
class QProgressBar;
class QPushButton;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(ChannelManager* mgr, QWidget* parent = nullptr);

    Inspector* inspector() const { return m_inspector; }

signals:
    void languageRequested(const QString& code);

protected:
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    QWidget* buildHeader();
    QWidget* buildControlPanel();
    QWidget* buildStatus();
    void refreshCounts();
    void refreshActive();
    void refreshRecording();
    void transportSelected(Transport t);
    void removeSelected();
    void tickSecond();
    void notice(const QString& text);
    void showDiagnostics();

    ChannelManager* m_mgr;
    ChannelGrid* m_grid;
    Inspector* m_inspector;
    QWidget* m_right;
    static constexpr int HeaderH = 72, StatusH = 22, RightMin = 384, RightMax = 560;

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
    QLabel* m_write;
    QLabel* m_hwEnc;
    bool m_closing = false;

    QLabel* m_selCap;
    KeyButton* m_btnTransport[4]{};
    KeyButton* m_btnSelect[5]{};
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
