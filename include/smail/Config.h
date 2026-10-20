#pragma once
#include <QString>
#include <QStringList>
#include <stdexcept>

namespace smail {
inline void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
struct Config {
    QString email, secret, outputDir;
    QString imapHost = "imap.qq.com", smtpHost = "smtp.qq.com";
    int imapPort = 993, smtpPort = 465, interval = 60;
    int maxAttachmentMb = 25, maxMessageMb = 50;
    QStringList extensions = {"pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "jpg", "jpeg", "png"};
    QStringList mimeTypes;
    bool strict = false, inlineImages = false, includeExisting = false, reply = false;
    bool rememberSecret = true;
    QString rejection = QStringLiteral("附件未被收取：{reason}。支持的类型：{allowed_types}。");
    QString validate() const;
    QString accountKey() const;
    void save(const QString &path) const;
    static Config load(const QString &path);
};
}
