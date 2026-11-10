#pragma once
#include <QWidget>

class Channel;
class ChannelManager;
class MeterPane;
class PreviewPane;
class QComboBox;
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
    void commitOutput();
    QMetaObject::Connection m_conn;

    ChannelManager* m_mgr;
    QLabel* m_title;
    PreviewPane* m_preview;
    MeterPane* m_meters;
    QPushButton* m_tabs[3]{};
    QStackedWidget* m_stack;
    QLineEdit* m_name;
    QComboBox* m_type;
    QComboBox* m_pattern;
    QComboBox* m_wave;
    QComboBox* m_format;
    QWidget* m_inputPage = nullptr;
    QWidget* m_outputForm = nullptr;
    QLabel* m_lock = nullptr;
    QLineEdit* m_outDir;
    QLineEdit* m_outTemplate;
    QComboBox* m_outEncoder;
    QComboBox* m_outVideoRate;
    QComboBox* m_outAudioRate;
    QComboBox* m_outContainer;
    QComboBox* m_outSplit;
    QPushButton* m_applySelected;
};
