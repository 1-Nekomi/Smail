#include "smail/Mime.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>

namespace smail {
namespace {
QByteArray unquotePrintable(const QByteArray &in, bool word = false) {
    QByteArray out;
    auto hex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
    for (qsizetype i = 0; i < in.size(); ++i) {
        if (word && in[i] == '_') out += ' ';
        else if (in[i] == '=' && in.mid(i + 1, 2) == "\r\n") i += 2;
        else if (in[i] == '=' && i + 1 < in.size() && in[i+1] == '\n') ++i;
        else if (in[i] == '=' && i + 2 < in.size() && hex(in[i+1]) && hex(in[i+2])) {
            out += QByteArray::fromHex(in.mid(i+1, 2)); i += 2;
        } else out += in[i];
    }
    return out;
}
QString text(const QByteArray &data, const QByteArray &charset) {
    QStringDecoder decoder(charset.constData());
    return decoder.isValid() ? decoder(data) : QString::fromUtf8(data);
}
QMap<QByteArray,QByteArray> headers(const QByteArray &head) {
    QMap<QByteArray,QByteArray> h;
    QByteArray key;
    for (auto line : head.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if ((line.startsWith(' ') || line.startsWith('\t')) && !key.isEmpty()) h[key] += " " + line.trimmed();
        else {
            auto colon = line.indexOf(':');
            if (colon <= 0) { key.clear(); continue; }
            key = line.left(colon).toLower();
            if (h.contains(key)) {
                if (key == "content-type" || key == "content-disposition" || key == "content-transfer-encoding"
                    || key == "from" || key == "auto-submitted" || key == "return-path")
                    fail("重复关键 MIME 标头，拒绝解析");
                // Received, DKIM-Signature and other trace headers legitimately repeat.
                h[key] += "\n" + line.mid(colon+1).trimmed();
            } else h[key] = line.mid(colon+1).trimmed();
        }
    }
    return h;
}
QString parameter(const QByteArray &header, const QString &key) {
    // RFC 2231 extended parameters and contiguous continuations.
    QMap<QString,QString> params;
    QRegularExpression re(R"rx(;\s*([\w*.-]+)\s*=\s*(?:"((?:\\.|[^"])*)"|([^;\s]+)))rx");
    auto it = re.globalMatch(QString::fromLatin1(header));
    while (it.hasNext()) {
        auto m = it.next();
        QString value = m.captured(2).isNull() ? m.captured(3) : m.captured(2);
        value.replace(QRegularExpression(R"(\\(.))"), "\\1");
        params[m.captured(1).toLower()] = value;
    }
    QString extended = params.value(key + "*");
    bool encoded = !extended.isEmpty();
    if (extended.isEmpty()) {
        for (int i = 0; i < 100; ++i) {
            auto k = key + "*" + QString::number(i);
            if (params.contains(k + "*")) { extended += params[k+"*"]; encoded = true; }
            else if (params.contains(k)) extended += params[k];
            else break;
        }
    }
    if (!extended.isEmpty()) {
        if (!encoded) return decodeHeader(extended.toLatin1());
        auto a = extended.indexOf('\''); auto b = extended.indexOf('\'', a+1);
        QByteArray charset = "utf-8";
        if (a >= 0 && b > a) { charset = extended.left(a).toLatin1(); extended = extended.mid(b+1); }
        return text(QByteArray::fromPercentEncoding(extended.toLatin1()), charset);
    }
    return decodeHeader(params.value(key).toLatin1());
}
void parsePart(const QByteArray &raw, Message &result, bool includeInline, int depth, int &count) {
    if (depth > 20 || ++count > 1000) fail("MIME 层级或数量超限");
    auto split = raw.indexOf("\r\n\r\n"); int sep = 4;
    if (split < 0) { split = raw.indexOf("\n\n"); sep = 2; }
    if (split < 0) fail("邮件 MIME 缺少头部结束标记");
    auto h = headers(raw.left(split));
    if (depth == 0) result.headers = h;
    auto body = raw.mid(split + sep);
    auto type = h.value("content-type", "text/plain").split(';').first().trimmed().toLower();
    auto disposition = h.value("content-disposition").split(';').first().trimmed().toLower();
    auto name = parameter(h.value("content-disposition"), "filename");
    if (name.isEmpty()) name = parameter(h.value("content-type"), "name");
    bool attachment = disposition == "attachment" || (!name.isEmpty() && disposition != "inline")
        || (includeInline && (disposition == "inline" || type.startsWith("image/")) && !type.startsWith("multipart/"));
    if (attachment) {
        QByteArray data;
        auto encoding = h.value("content-transfer-encoding").toLower();
        if (encoding == "base64") {
            QByteArray compact = body;
            compact.replace("\r", ""); compact.replace("\n", ""); compact.replace(" ", ""); compact.replace("\t", "");
            auto decoded = QByteArray::fromBase64Encoding(compact, QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) fail("附件 Base64 损坏");
            data = decoded.decoded;
        } else if (encoding == "quoted-printable") data = unquotePrintable(body);
        else if (encoding.isEmpty() || encoding == "7bit" || encoding == "8bit" || encoding == "binary") data = body;
        else fail("不支持的附件传输编码");
        if (name.isEmpty()) {
            QString ext = type == "image/jpeg" ? "jpg" : type == "image/png" ? "png" : "bin";
            name = "attachment." + ext;
        }
        result.attachments.push_back({name, QString::fromLatin1(type), data});
    } else if (type.startsWith("multipart/")) {
        auto boundary = parameter(h.value("content-type"), "boundary").toLatin1();
        if (boundary.isEmpty() || boundary.size() > 200) fail("MIME boundary 无效");
        QByteArray part; bool active = false, closed = false;
        for (auto line : body.split('\n')) {
            auto marker = line.trimmed();
            if (marker == "--" + boundary || marker == "--" + boundary + "--") {
                if (active) {
                    if (part.endsWith("\r\n")) part.chop(2); else if (part.endsWith('\n')) part.chop(1);
                    parsePart(part, result, includeInline, depth+1, count);
                }
                part.clear(); active = true;
                if (marker.endsWith("--") && marker == "--" + boundary + "--") { closed = true; break; }
            } else if (active) part += line + '\n';
        }
        if (!closed) fail("MIME multipart 不完整");
    }
}
}
QString decodeHeader(const QByteArray &value) {
    QString input = QString::fromUtf8(value), out;
    QRegularExpression re(R"(=\?([^?]+)\?([bBqQ])\?([^?]*)\?=)");
    auto it = re.globalMatch(input); qsizetype pos = 0; bool previous = false;
    while (it.hasNext()) {
        auto m = it.next(); auto gap = input.mid(pos, m.capturedStart()-pos);
        if (!previous || !gap.trimmed().isEmpty()) out += gap;
        auto data = m.captured(3).toLatin1();
        out += text(m.captured(2).toLower() == "b" ? QByteArray::fromBase64(data) : unquotePrintable(data, true), m.captured(1).toLatin1());
        pos = m.capturedEnd(); previous = true;
    }
    return out + input.mid(pos);
}
Message parseMessage(const QByteArray &raw, bool includeInline) {
    Message m; int count = 0; parsePart(raw, m, includeInline, 0, count); return m;
}
Selection selectAttachments(const Message &m, const Config &c) {
    Selection s;
    if (m.attachments.isEmpty()) { s.status = "ignored"; s.reason = "无候选附件"; return s; }
    int bad = 0;
    for (const auto &a : m.attachments) {
        bool ext = c.extensions.isEmpty() || c.extensions.contains(QFileInfo(a.name).suffix().toLower());
        bool mime = c.mimeTypes.isEmpty();
        for (const auto &rule : c.mimeTypes)
            if (rule == a.mime || (rule.endsWith("/*") && a.mime.startsWith(rule.left(rule.size()-1)))) mime = true;
        if (ext && mime && a.data.size() <= qint64(c.maxAttachmentMb)*1024*1024) s.accepted << a;
        else ++bad;
    }
    if ((c.strict && bad) || s.accepted.isEmpty()) {
        s.accepted.clear(); s.status = "rejected"; s.reason = "附件类型不符合规则或大小超限";
    } else { s.status = "saved"; s.reason = QString("已保存 %1 个附件，过滤 %2 个").arg(s.accepted.size()).arg(bad); }
    return s;
}
QString safeName(QString name) {
    name.replace(QRegularExpression(R"([<>:"/\\|?*\x00-\x1f])"), "_");
    name = name.trimmed();
    while (name.endsWith('.') || name.endsWith(' ')) name.chop(1);
    if (name.isEmpty()) name = "attachment";
    if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])($|\\.)", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) name.prepend('_');
    if (name.size() > 120) name = name.left(95) + "." + QFileInfo(name).suffix().left(20);
    return name;
}
QString replyAddress(const Message &m, const QString &self) {
    const auto &h = m.headers;
    if ((!h.value("auto-submitted").isEmpty() && h.value("auto-submitted").toLower() != "no")
        || h.contains("list-id") || h.contains("list-unsubscribe") || h.contains("x-auto-response-suppress")
        || h.value("return-path") == "<>" || h.contains("precedence")) return {};
    QString from = QString::fromUtf8(h.value("from")).trimmed();
    static const QRegularExpression address(R"([A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})");
    auto matches = address.globalMatch(from);
    if (!matches.hasNext()) return {};
    auto match = matches.next(); QString result = match.captured();
    if (matches.hasNext() || result.compare(self, Qt::CaseInsensitive) == 0
        || result.contains(QRegularExpression("no.?reply|mailer-daemon|postmaster", QRegularExpression::CaseInsensitiveOption))) return {};
    // Accept a bare address or one display-name mailbox enclosed in angle brackets.
    if (from != result && !from.endsWith("<" + result + ">")) return {};
    return result;
}
}
