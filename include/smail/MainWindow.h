#pragma once
#include "smail/Config.h"
#include <QMainWindow>
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QSystemTrayIcon;
class QTabWidget;
namespace smail {
class Collector;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QString dataDir);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    QString dataDir;
    QLineEdit *email, *secret, *output, *imap, *smtp, *extensions, *mimeTypes;
    QSpinBox *imapPort, *smtpPort, *interval, *attachmentSize, *messageSize;
    QCheckBox *strict, *inlineImages, *existing, *reply;
    QComboBox *provider;
    QPlainTextEdit *rejection, *log, *history;
    QPushButton *start, *stop;
    QLabel *status;
    QTabWidget *tabs;
    QSystemTrayIcon *tray;
    Collector *collector = nullptr;
    Config readConfig() const;
    void fill(const Config &config);
    void begin();
    void appendLog(const QString &message);
    void refreshHistory();
};
}
