#include "smail/MailClient.h"
#include "smail/TlsConnection.h"
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QUuid>
#include <QDateTime>
#include <QLocale>
#include <algorithm>

namespace smail {
namespace {
QByteArray quoted(QString value) {
    if (value.contains('\r') || value.contains('\n') || value.contains(QChar(0))) fail("协议参数包含非法字符");
    value.replace("\\", "\\\\"); value.replace("\"", "\\\"");
    return '"' + value.toUtf8() + '"';
}
}
MailClient::MailClient(std::function<bool()> cancel) : cancelled(std::move(cancel)) {}
void MailClient::checkCancelled() { if (cancelled && cancelled()) fail("收取已停止"); }
void MailClient::connectTls(const QString &host, int port) {
    establishTls(socket, host, quint16(port), cancelled);
}
void MailClient::write(const QByteArray &data) {
    checkCancelled();
    if (socket.write(data) != data.size()) fail("网络写入失败");
    QElapsedTimer timer; timer.start();
    while (socket.bytesToWrite()) {
        checkCancelled();
        socket.waitForBytesWritten(250);
        if (socket.state() != QAbstractSocket::ConnectedState || timer.elapsed() > 30000) fail("网络发送超时");
    }
}
QByteArray MailClient::line() {
    QElapsedTimer timer; timer.start();
    while (!socket.canReadLine()) {
        checkCancelled();
        if (socket.bytesAvailable() > 1024*1024) fail("服务器响应行超限");
        socket.waitForReadyRead(250);
        if (socket.state() != QAbstractSocket::ConnectedState || timer.elapsed() > 30000) fail("服务器响应超时或连接已断开");
    }
    auto result = socket.readLine(1024*1024+1);
    if (!result.endsWith('\n')) fail("服务器响应行超限");
    return result;
}
QByteArray MailClient::bytes(qint64 size) {
    QByteArray out; out.reserve(size);
    QElapsedTimer timer; timer.start();
    while (out.size() < size) {
        checkCancelled();
        auto chunk = socket.read(qMin<qint64>(size-out.size(), 65536));
        if (!chunk.isEmpty()) out += chunk;
        else socket.waitForReadyRead(250);
        if (timer.elapsed() > 120000 || (socket.state() != QAbstractSocket::ConnectedState && socket.bytesAvailable() == 0 && out.size() < size))
            fail("邮件正文下载超时或连接断开");
    }
    return out;
}
ImapResponse MailClient::command(const QByteArray &cmd, qint64 limit) {
    QByteArray tag = "S" + QByteArray::number(++serial);
    write(tag + " " + cmd + "\r\n");
    ImapResponse response; qint64 total = 0;
    QRegularExpression literal(R"(\{(\d+)\}\r?\n$)");
    QElapsedTimer timer; timer.start();
    while (true) {
        if (timer.elapsed() > 180000) fail("IMAP 命令超时");
        auto l = line();
        if (l.startsWith(tag + " ")) {
            if (!l.startsWith(tag + " OK")) fail("IMAP 命令失败，请检查授权码、服务开关或邮箱权限");
            return response;
        }
        if (l.startsWith("* BYE")) fail("IMAP 服务器关闭连接");
        response.lines += l;
        if (response.lines.size() > 8*1024*1024) fail("IMAP 元数据超限");
        auto m = literal.match(QString::fromLatin1(l));
        if (m.hasMatch()) {
            bool ok = false; qint64 n = m.captured(1).toLongLong(&ok);
            if (!ok || n > limit-total) fail("IMAP 正文超过下载限制");
            total += n; response.literals << bytes(n);
        }
    }
}
void MailClient::login(const Config &c) {
    connectTls(c.imapHost, c.imapPort);
    auto greeting = line();
    if (!greeting.startsWith("* OK")) fail("IMAP 欢迎响应异常");
    command("LOGIN " + quoted(c.email) + " " + quoted(c.secret));
}
QString MailClient::selectInbox() {
    auto r = command("SELECT INBOX");
    auto m = QRegularExpression(R"(\[UIDVALIDITY (\d+)\])", QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(r.lines));
    if (!m.hasMatch()) fail("服务器未返回 UIDVALIDITY");
    return m.captured(1);
}
QVector<quint64> MailClient::parseSearch(const QByteArray &lines, quint64 after) {
    QVector<quint64> result;
    for (auto line : lines.split('\n')) {
        line = line.trimmed();
        if (line == "* SEARCH") continue;
        if (!line.startsWith("* SEARCH ")) continue;
        for (const auto &part : line.mid(9).split(' ')) {
            if (part.isEmpty()) continue;
            bool ok; auto n = part.toULongLong(&ok);
            if (!ok || n > 4294967295ULL || !n) fail("服务器返回非法 UID");
            if (n > after) result << n; // IMAP n:* can include the highest UID below n.
        }
    }
    std::sort(result.begin(), result.end()); result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
QVector<quint64> MailClient::search(quint64 after) {
    if (after >= 4294967295ULL) return {};
    return parseSearch(command(after ? "UID SEARCH UID " + QByteArray::number(after+1) + ":*" : "UID SEARCH ALL").lines, after);
}
QVector<quint64> MailClient::searchUnread() {
    return parseSearch(command("UID SEARCH UNSEEN").lines, 0);
}
qint64 MailClient::messageSize(quint64 uid) {
    auto r = command("UID FETCH " + QByteArray::number(uid) + " (RFC822.SIZE)");
    auto m = QRegularExpression(R"(RFC822\.SIZE (\d+))", QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(r.lines));
    return m.hasMatch() ? m.captured(1).toLongLong() : -1;
}
QByteArray MailClient::fetch(quint64 uid, qint64 limit) {
    auto r = command("UID FETCH " + QByteArray::number(uid) + " (BODY.PEEK[])", limit);
    if (r.literals.size() != 1) fail("邮件已消失或正文响应异常，稍后重试");
    return r.literals.first();
}
void MailClient::markRead(quint64 uid) { command("UID STORE " + QByteArray::number(uid) + " +FLAGS.SILENT (\\Seen)"); }
QByteArray MailClient::fetchReplyHeaders(quint64 uid) {
    auto r = command("UID FETCH " + QByteArray::number(uid) +
        " (BODY.PEEK[HEADER.FIELDS (FROM AUTO-SUBMITTED LIST-ID LIST-UNSUBSCRIBE X-AUTO-RESPONSE-SUPPRESS RETURN-PATH PRECEDENCE)])", 65536);
    if (r.literals.size() != 1) fail("无法读取回复诊断所需的邮件头部");
    return r.literals.first();
}
int MailClient::smtpResponse() {
    int code = 0;
    for (int count = 0; count < 100; ++count) {
        auto l = line();
        if (l.size() < 5) fail("SMTP 响应无效");
        bool ok; int current = l.left(3).toInt(&ok);
        if (!ok || (code && current != code)) fail("SMTP 响应无效");
        code = current;
        lastSmtpResponse = l.trimmed();
        if (l[3] == ' ') return code;
        if (l[3] != '-') fail("SMTP 响应无效");
    }
    fail("SMTP 响应过长"); return 0;
}
void MailClient::smtp(const QByteArray &cmd, int expected) {
    write(cmd + "\r\n");
    const int code = smtpResponse();
    if (code != expected)
        fail(QString("SMTP 操作失败（服务器返回 %1，期望 %2）：%3")
                 .arg(code).arg(expected).arg(QString::fromUtf8(lastSmtpResponse)));
}
void MailClient::smtpConnectAndLogin(const Config &c) {
    try {
        connectTls(c.smtpHost, c.smtpPort);
    } catch (const std::exception &e) {
        const QString detail = QString::fromUtf8(e.what());
        if (detail.contains("收取已停止")) fail(detail);
        // SMTP is commonly blocked by campus networks, firewalls and VPN/proxy
        // rules while IMAP still works. Make that actionable instead of
        // reporting only "Remote host closed".
        fail(detail + QString("；请检查网络、防火墙或 VPN/代理是否允许访问 SMTP %1:%2"
                              "（QQ 邮箱常用 465；若使用 Clash 等全局代理，请切换到规则/直连模式，"
                              "或将 %1 设为直连，也可暂时关闭代理）")
                           .arg(c.smtpHost).arg(c.smtpPort));
    }
    if (smtpResponse() != 220) fail("SMTP 服务未就绪（未收到 220 欢迎响应）");
    smtp("EHLO smail.local", 250);
    smtp("AUTH LOGIN", 334);
    smtp(c.email.toUtf8().toBase64(), 334);
    smtp(c.secret.toUtf8().toBase64(), 235);
}
void MailClient::sendReply(const Config &c, const QString &to, const QString &body, std::function<bool()> cancel) {
    if (to.contains('\r') || to.contains('\n') || to.contains('<') || to.contains('>')) fail("回复地址无效");
    MailClient client(std::move(cancel));
    client.smtpConnectAndLogin(c);
    client.smtp("MAIL FROM:<" + c.email.toUtf8() + ">", 250);
    client.smtp("RCPT TO:<" + to.toUtf8() + ">", 250);
    client.smtp("DATA", 354);
    QString domain = c.email.section('@', 1).trimmed();
    if (!QRegularExpression("^[A-Za-z0-9.-]+\\.[A-Za-z]{2,}$").match(domain).hasMatch())
        domain = "smail.local";
    QByteArray message = "From: <" + c.email.toUtf8() + ">\r\nTo: <" + to.toUtf8()
        + ">\r\nSubject: =?UTF-8?B?" + QStringLiteral("Smail 附件收取结果").toUtf8().toBase64()
        + "?=\r\nDate: " + QLocale::c().toString(QDateTime::currentDateTimeUtc(), "ddd, dd MMM yyyy HH:mm:ss '+0000'").toLatin1()
        + "\r\nMessage-ID: <" + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1()
        + "@" + domain.toLatin1() + ">\r\nAuto-Submitted: auto-replied\r\nX-Auto-Response-Suppress: All\r\n"
          "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n";
    auto encoded = body.toUtf8().toBase64();
    for (qsizetype i = 0; i < encoded.size(); i += 76) message += encoded.mid(i,76) + "\r\n";
    client.write(message + ".\r\n");
    const int confirmation = client.smtpResponse();
    if (confirmation != 250)
        fail(QString("SMTP 未确认接收回复（服务器返回 %1）：%2")
                 .arg(confirmation).arg(QString::fromUtf8(client.lastSmtpResponse)));
    // The reply is already accepted; a failed QUIT must not turn it into an
    // "uncertain" result.
    try { client.write("QUIT\r\n"); client.smtpResponse(); } catch (const std::exception &) {}
}
void MailClient::testSmtp(const Config &c, std::function<bool()> cancel) {
    MailClient client(std::move(cancel));
    client.smtpConnectAndLogin(c);
    try { client.write("QUIT\r\n"); client.smtpResponse(); } catch (const std::exception &) {}
}
}
