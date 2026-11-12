#pragma once
#include <QDialog>

class QComboBox;
class QLineEdit;

// Ustawienia programu: domyslne parametry nagrywania dla nowych kanalow, karta dzwiekowa odsluchu,
// szablon nazwy folderu dla "Split to new folder".
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    void save();

    QLineEdit* m_dir;
    QLineEdit* m_template;
    QComboBox* m_container;
    QComboBox* m_split;
    QComboBox* m_encoder;
    QComboBox* m_videoRate;
    QComboBox* m_audioRate;
    QComboBox* m_monitor;
    QLineEdit* m_folderTemplate;
    QComboBox* m_rename;
};

// Opis zmiennych szablonu nazwy (podpowiedz przy etykietach "Name template").
QString nameTemplateHelp();
