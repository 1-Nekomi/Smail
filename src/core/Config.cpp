#include "smail/Config.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace smail {
QString Config::validate() const {
    static const QRegularExpression mail(R"(^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    static const QRegularExpression host(R"(^[A-Za-z0-9.-]+$)");
    if (!mail.match(email).hasMatch()) return QStringLiteral("请输入有效邮箱地址。");
    if (secret.isEmpty() || secret.contains('\r') || secret.contains('\n')) return QStringLiteral("请输入邮箱授权码。");
    if (!host.match(imapHost).hasMatch() || !host.match(smtpHost).hasMatch()) return QStringLiteral("服务器地址只能包含域名或 IPv4 地址。");
    if (imapPort < 1 || imapPort > 65535 || smtpPort < 1 || smtpPort > 65535) return QStringLiteral("端口不合法。");
    if (outputDir.trimmed().isEmpty()) return QStringLiteral("请选择附件目录。");
    if (interval < 15 || interval > 3600) return QStringLiteral("轮询间隔应为 15–3600 秒。");
    if (maxAttachmentMb < 1 || maxMessageMb < maxAttachmentMb || maxMessageMb > 200) return QStringLiteral("大小限制应满足：1 ≤ 附件 ≤ 邮件 ≤ 200 MB。");
    if (reply && rejection.trimmed().isEmpty()) return QStringLiteral("请填写拒收回复内容。");
    if (withAttachmentReply && withAttachmentMessage.trimmed().isEmpty()) return QStringLiteral("请填写有附件时的回复内容。");
    if (withoutAttachmentReply && withoutAttachmentMessage.trimmed().isEmpty()) return QStringLiteral("请填写无附件时的回复内容。");
    for (const auto &ext : extensions)
        if (!QRegularExpression("^[a-z0-9]+$").match(ext).hasMatch()) return QStringLiteral("扩展名应为 pdf、docx 等，用逗号分隔。");
    for (const auto &mime : mimeTypes)
        if (!QRegularExpression("^[a-z0-9.+-]+/([a-z0-9.+-]+|\\*)$").match(mime).hasMatch()) return QStringLiteral("MIME 类型格式应为 image/* 或 application/pdf。");
    return {};
}
QString Config::accountKey() const {
    return QString::fromLatin1(QCryptographicHash::hash(
        (imapHost.toLower() + ":" + QString::number(imapPort) + "/" + email.toLower()).toUtf8(),
        QCryptographicHash::Sha256).toHex().left(24));
}
void Config::save(const QString &path) const {
    QJsonObject o{{"email", email}, {"outputDir", outputDir}, {"imapHost", imapHost},
        {"smtpHost", smtpHost}, {"imapPort", imapPort}, {"smtpPort", smtpPort},
        {"interval", interval}, {"maxAttachmentMb", maxAttachmentMb}, {"maxMessageMb", maxMessageMb},
        {"extensions", QJsonArray::fromStringList(extensions)}, {"mimeTypes", QJsonArray::fromStringList(mimeTypes)},
        {"strict", strict}, {"inlineImages", inlineImages}, {"includeExisting", includeExisting},
        {"reply", reply}, {"rejection", rejection}, {"rememberSecret", rememberSecret},
        {"withAttachmentReply", withAttachmentReply}, {"withoutAttachmentReply", withoutAttachmentReply},
        {"withAttachmentMessage", withAttachmentMessage}, {"withoutAttachmentMessage", withoutAttachmentMessage}};
    QSaveFile f(path);
    const auto data = QJsonDocument(o).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) fail("无法保存配置");
}
Config Config::load(const QString &path) {
    Config c;
    QFile f(path);
    if (!f.exists()) return c;
    if (!f.open(QIODevice::ReadOnly)) fail("无法读取配置");
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject()) fail("配置 JSON 无效");
    auto o = d.object();
    c.email = o["email"].toString(); c.outputDir = o["outputDir"].toString();
    c.imapHost = o["imapHost"].toString(c.imapHost); c.smtpHost = o["smtpHost"].toString(c.smtpHost);
    c.imapPort = o["imapPort"].toInt(993); c.smtpPort = o["smtpPort"].toInt(465);
    c.interval = o["interval"].toInt(60); c.maxAttachmentMb = o["maxAttachmentMb"].toInt(25);
    c.maxMessageMb = o["maxMessageMb"].toInt(50);
    if (o.contains("extensions")) { c.extensions.clear(); for (auto v : o["extensions"].toArray()) c.extensions << v.toString(); }
    for (auto v : o["mimeTypes"].toArray()) c.mimeTypes << v.toString();
    c.strict = o["strict"].toBool(); c.inlineImages = o["inlineImages"].toBool();
    c.includeExisting = o["includeExisting"].toBool(); c.reply = o["reply"].toBool();
    c.rejection = o["rejection"].toString(c.rejection);
    c.rememberSecret = o["rememberSecret"].toBool(true);
    c.withAttachmentReply = o["withAttachmentReply"].toBool(false);
    c.withoutAttachmentReply = o["withoutAttachmentReply"].toBool(false);
    c.withAttachmentMessage = o["withAttachmentMessage"].toString(c.withAttachmentMessage);
    c.withoutAttachmentMessage = o["withoutAttachmentMessage"].toString(c.withoutAttachmentMessage);
    return c;
}
}
