#pragma once
#include <QDialog>
#include <QStringList>

class ChannelManager;
class DiskTest;
class QLabel;
class QTableWidget;
struct DiskTestResult;

// Lista wolumenow uzywanych przez kanaly: wolne miejsce, potrzebny strumien, czas do zapelnienia, test zapisu.
class DisksDialog : public QDialog {
    Q_OBJECT
public:
    explicit DisksDialog(ChannelManager* mgr, QWidget* parent = nullptr);
    ~DisksDialog() override;
    static QString resultText(const DiskTestResult& r, double requiredMbps);

private:
    void refresh();
    void startTest(const QString& root, int seconds);

    ChannelManager* m_mgr;
    QTableWidget* m_table;
    QLabel* m_status;
    QStringList m_roots;        // wiersze tabeli
    DiskTest* m_test = nullptr;
    QString m_testRoot;
    QString m_testProgress;
};
