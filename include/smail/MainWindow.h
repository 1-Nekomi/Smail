#pragma once
#include "smail/Config.h"
#include <QMainWindow>
#include <atomic>
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QSystemTrayIcon;
class QTabWidget;
class QThread;
class QListWidget;
namespace smail {
class Collector;
class ReplyBodyEditor;
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
    QCheckBox *strict, *inlineImages, *existing, *reply, *rememberSecret;
    QCheckBox *withAttachmentReply, *withoutAttachmentReply;
    QComboBox *provider;
    QPlainTextEdit *log, *history;
    ReplyBodyEditor *rejection, *withAttachmentMessage, *withoutAttachmentMessage;
    QLineEdit *withAttachmentSubject, *withoutAttachmentSubject, *rejectionSubject;
    QListWidget *withAttachmentFiles, *withoutAttachmentFiles, *rejectionFiles;
    QPushButton *start, *stop;
    QLabel *status;
    QTabWidget *tabs;
    QSystemTrayIcon *tray;
    Collector *collector = nullptr;
    QThread *smtpProbe = nullptr;
    std::atomic_bool smtpProbeCancelled{false};
    Config readConfig() const;
    void fill(const Config &config);
    void begin();
    void appendLog(const QString &message);
    void refreshHistory();
    void loadSecret();
    void persistSettings(const Config &config);
};
}
