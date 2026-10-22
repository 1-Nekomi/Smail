// Read-only SMTP diagnostic: connects and authenticates, never sends a message.
#include "smail/Config.h"
#include "smail/CredentialStore.h"
#include "smail/MailClient.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Smail");
    QCoreApplication::setApplicationName("Smail");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"data-dir", "Override local configuration/state directory.", "directory"});
    parser.process(app);
    const QString path = parser.isSet("data-dir")
        ? QDir(parser.value("data-dir")).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QTextStream out(stdout), err(stderr);
    try {
        auto config = smail::Config::load(path + "/settings.json");
        config.secret = smail::CredentialStore(path + "/credentials").load(config);
        if (config.secret.isEmpty()) {
            err << "没有找到已保存的授权码。请先在程序中保存设置并勾选“加密记住授权码”。\n";
            return 1;
        }
        out << "数据目录：" << path << "\n";
        out << "SMTP：" << config.smtpHost << ":" << config.smtpPort << "\n";
        out << "正在连接并登录（不会发送邮件）…\n";
        out.flush();
        smail::MailClient::testSmtp(config);
        out << "成功：SMTP 连接与登录正常。\n";
        return 0;
    } catch (const std::exception &error) {
        err << "失败：" << QString::fromUtf8(error.what()) << "\n";
        return 1;
    }
}
