#include "smail/ReplyMessage.h"
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QUuid>

namespace smail {
namespace {
QByteArray base64Lines(const QByteArray &data) {
    const auto encoded = data.toBase64();
    QByteArray result;
    for (qsizetype i = 0; i < encoded.size(); i += 76) result += encoded.mid(i, 76) + "\r\n";
    return result;
}
QByteArray encodedSubject(const QString &subject) {
    // Keep each RFC 2047 encoded word below 75 bytes; do not split UTF-8 characters.
    QByteArray result, chunk;
    for (char32_t codepoint : subject.toUcs4()) {
        const auto bytes = QString::fromUcs4(&codepoint, 1).toUtf8();
        if (chunk.size() + bytes.size() > 42) {
            result += "=?UTF-8?B?" + chunk.toBase64() + "?=\r\n "; chunk.clear();
        }
        chunk += bytes;
    }
    return result + "=?UTF-8?B?" + chunk.toBase64() + "?=";
}
QByteArray filenameParameter(const QString &name) {
    // Percent-encode every byte so continuation boundaries always follow a full %XX token.
    QByteArray encoded;
    for (auto byte : name.toUtf8()) encoded += '%' + QByteArray(1, byte).toHex().toUpper();
    QByteArray result;
    for (qsizetype offset = 0, index = 0; offset < encoded.size(); offset += 45, ++index)
        result += ";\r\n filename*" + QByteArray::number(index) + "*="
            + (index == 0 ? QByteArray("UTF-8''") : QByteArray()) + encoded.mid(offset, 45);
    return result;
}
}
QString validateReplySubject(const QString &subject) {
    if (subject.trimmed().isEmpty()) return "请填写回复邮件主题。";
    if (subject.size() > 200) return "回复主题最多 200 个字符。";
    for (auto ch : subject)
        if (ch.unicode() < 32 || ch.unicode() == 127) return "回复主题不能包含换行或控制字符。";
    return {};
}
QString validateReplyFiles(const QStringList &paths) {
    if (paths.size() > 20) return "每种回复最多选择 20 个附件。";
    qint64 total = 0;
    for (const auto &path : paths) {
        QFileInfo info(path);
        if (!info.isAbsolute() || !info.isFile() || !info.isReadable()) return "回复附件不存在或不可读：" + path;
        total += info.size();
        if (total > ReplyFilesLimit) return "每封回复的附件合计不能超过 25 MB。";
    }
    return {};
}
QByteArray composeReply(const QString &from, const QString &to, const QString &subject,
                        const QString &body, const QStringList &paths) {
    static const QRegularExpression address(R"(^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    if (!address.match(from).hasMatch() || !address.match(to).hasMatch()) fail("回复地址无效");
    auto error = validateReplySubject(subject);
    if (!error.isEmpty()) fail(error);
    error = validateReplyFiles(paths);
    if (!error.isEmpty()) fail(error);
    if (body.toUtf8().size() > 1024*1024) fail("回复正文不能超过 1 MB");
    QByteArray message = "From: <" + from.toUtf8() + ">\r\nTo: <" + to.toUtf8()
        + ">\r\nSubject: " + encodedSubject(subject)
        + "\r\nDate: " + QLocale::c().toString(QDateTime::currentDateTimeUtc(), "ddd, dd MMM yyyy HH:mm:ss '+0000'").toLatin1()
        + "\r\nMessage-ID: <" + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1()
        + "@" + from.section('@', 1).toLatin1() + ">\r\nAuto-Submitted: auto-replied\r\nX-Auto-Response-Suppress: All\r\nMIME-Version: 1.0\r\n";
    const QByteArray textPart = "Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n" + base64Lines(body.toUtf8());
    if (paths.isEmpty()) return message + textPart;
    const auto boundary = "smail_" + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
    message += "Content-Type: multipart/mixed; boundary=\"" + boundary + "\"\r\n\r\n--" + boundary + "\r\n" + textPart;
    qint64 remaining = ReplyFilesLimit;
    QMimeDatabase types;
    for (const auto &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) fail("无法读取回复附件：" + path);
        auto bytes = file.read(remaining + 1);
        if (file.error() != QFileDevice::NoError) fail("读取回复附件失败：" + path);
        if (bytes.size() > remaining || !file.atEnd()) fail("回复附件合计超过 25 MB：" + path);
        remaining -= bytes.size();
        const auto name = QFileInfo(path).fileName();
        message += "--" + boundary + "\r\nContent-Type: " + types.mimeTypeForFileNameAndData(name, bytes).name().toLatin1()
            + "\r\nContent-Disposition: attachment" + filenameParameter(name)
            + "\r\nContent-Transfer-Encoding: base64\r\n\r\n" + base64Lines(bytes);
        if (bytes.isEmpty()) message += "\r\n";
    }
    return message + "--" + boundary + "--\r\n";
}
}
