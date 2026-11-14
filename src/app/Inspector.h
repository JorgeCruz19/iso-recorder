#pragma once
#include <QHash>
#include <QWidget>

#include "core/Schedule.h"

class Channel;
class ChannelManager;
class MeterPane;
class PreviewPane;
class QCheckBox;
class QComboBox;
class QDateEdit;
class QListWidget;
class QTimeEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

// Prawy panel: ustawienia kanalu aktywnego (zakladki Input / Output / Scheduler).
class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(ChannelManager* mgr, QWidget* parent = nullptr);
    void setTab(int index);

private:
    void bind();
    void refresh();
    QWidget* buildInputPage();
    QWidget* buildOutputPage();
    QWidget* buildSchedulePage();
    void commitSchedule();          // edytor reguly -> kanal
    void refreshSchedule();         // kanal -> lista regul, edytor, status
    void fillRuleEditor();
    QString ruleText(const ScheduleRule& r) const;
    QLabel* m_schedStatus = nullptr;
    QListWidget* m_ruleList = nullptr;
    QPushButton* m_ruleAdd = nullptr;
    QPushButton* m_ruleDel = nullptr;
    QWidget* m_ruleEditor = nullptr;
    QComboBox* m_ruleRepeat = nullptr;
    QPushButton* m_ruleDays[7]{};
    QDateEdit* m_ruleDate = nullptr;
    QTimeEdit* m_ruleStart = nullptr;
    QTimeEdit* m_ruleStop = nullptr;
    QComboBox* m_ruleSplit = nullptr;
    QCheckBox* m_catchUp = nullptr;
    ScheduleConfig m_schedShown;    // reguly pokazane na liscie
    Channel* m_schedFor = nullptr;
    bool m_schedFilling = false;
    void commitOutput();
    void commitLive();
    QMetaObject::Connection m_conn;

    ChannelManager* m_mgr;
    QLabel* m_title;
    PreviewPane* m_preview;
    MeterPane* m_meters;
    QPushButton* m_tabs[3]{};
    QStackedWidget* m_stack;
    QLineEdit* m_name;
    QComboBox* m_type;
    QComboBox* m_device;
    QLineEdit* m_uri;
    QComboBox* m_camMode;
    QComboBox* m_loss;
    QComboBox* m_gain;
    void fillCameraModes(const QString& cameraPath);
    void fillDevices(const QString& type);
    void commitInput();
    QString suggestedName(Channel* ch) const;
    void offerRename(Channel* ch, const QString& name);
    QHash<Channel*, QString> m_autoNames;       // nazwy nadane kanalom automatycznie (wg zrodla) w tej sesji
    QComboBox* m_pattern;
    QComboBox* m_wave;
    QComboBox* m_format;
    QComboBox* m_fps;
    QPushButton* m_reconnect;
    QWidget* m_inputPage = nullptr;
    QWidget* m_outputForm = nullptr;
    QLabel* m_lock = nullptr;
    QLineEdit* m_outDir;
    QLineEdit* m_outTemplate;
    QComboBox* m_outEncoder;
    QComboBox* m_outCodec;
    QComboBox* m_outVideoRate;
    QComboBox* m_outAudioRate;
    QComboBox* m_outContainer;
    QComboBox* m_outSplit;
    QPushButton* m_applySelected;
    QComboBox* m_liveType;
    QLineEdit* m_liveNdi;
    QLineEdit* m_liveSrt;
    QComboBox* m_liveRate;
    QComboBox* m_liveWhen;
};
