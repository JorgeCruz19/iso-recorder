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

private:
    void bind();
    void refresh();
    QWidget* buildInputPage();
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
};
