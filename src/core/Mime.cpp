#include "smail/Mime.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>
#include <limits>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
QByteArray normalizeCharset(QByteArray charset) {
    charset = charset.trimmed();
    if (const auto star = charset.indexOf('*'); star >= 0) charset = charset.left(star).trimmed();
    if (charset.size() >= 2 && ((charset.startsWith('"') && charset.endsWith('"'))
                                || (charset.startsWith('\'') && charset.endsWith('\''))))
        charset = charset.mid(1, charset.size() - 2).trimmed();
    return charset.toLower();
}
bool isPureAscii(const QByteArray &data) {
    for (unsigned char c : data)
        if (c >= 0x80) return false;
    return true;
}
bool tryDecodeUtf8(const QByteArray &data, QString &out) {
    if (data.isEmpty()) { out.clear(); return true; }
    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    QString decoded = decoder.decode(data);
    if (decoder.hasError()) return false;
    out = std::move(decoded);
    return true;
}
#ifdef Q_OS_WIN
bool codePageSupportsStrict(UINT codePage) {
    if (codePage == 42 || codePage == 50220 || codePage == 50221 || codePage == 50222
        || codePage == 50225 || codePage == 50227 || codePage == 50229
        || codePage == 52936 || codePage == 65000)
        return false;
    if (codePage >= 57002 && codePage <= 57011) return false;
    return true;
}
bool tryDecodeCodePage(const QByteArray &data, UINT codePage, bool strict, QString &out) {
    if (data.isEmpty()) { out.clear(); return true; }
    if (data.size() > std::numeric_limits<int>::max()) return false;
    if (codePage != CP_UTF8 && !IsValidCodePage(codePage)) return false;
    const DWORD flags = (strict && codePageSupportsStrict(codePage)) ? MB_ERR_INVALID_CHARS : 0;
    const int needed = MultiByteToWideChar(codePage, flags, data.constData(), int(data.size()), nullptr, 0);
    if (needed <= 0) return false;
    QString result(needed, Qt::Uninitialized);
    const int written = MultiByteToWideChar(codePage, flags, data.constData(), int(data.size()),
                                            reinterpret_cast<wchar_t *>(result.data()), needed);
    if (written <= 0) return false;
    result.truncate(written);
    out = std::move(result);
    return true;
}
UINT windowsCodePage(const QByteArray &cs) {
    if (cs == "gb18030" || cs == "gb-18030" || cs == "gb_18030"
        || cs == "gbk" || cs == "x-gbk" || cs == "cp936" || cs == "ms936" || cs == "windows-936"
        || cs == "gb2312" || cs == "gb-2312" || cs == "gb_2312" || cs == "gb_2312-80"
        || cs == "gb2312-80" || cs == "gb2312-1980" || cs == "csgb2312" || cs == "csiso58gb231280"
        || cs == "iso-ir-58" || cs == "chinese" || cs == "euc-cn" || cs == "euccn" || cs == "cn-gb")
        return IsValidCodePage(54936) ? 54936 : 936;
    if (cs == "hz-gb-2312" || cs == "hz") return 52936;
    if (cs == "big5" || cs == "big-5" || cs == "big5-hkscs" || cs == "big5hkscs"
        || cs == "cn-big5" || cs == "csbig5" || cs == "x-x-big5"
        || cs == "cp950" || cs == "ms950" || cs == "windows-950")
        return 950;
    if (cs == "shift_jis" || cs == "shift-jis" || cs == "sjis" || cs == "x-sjis"
        || cs == "csshiftjis" || cs == "ms_kanji" || cs == "ms932" || cs == "cp932"
        || cs == "windows-31j" || cs == "windows-932")
        return 932;
    if (cs == "euc-jp" || cs == "x-euc-jp" || cs == "cseucpkdfmtjapanese") return 51932;
    if (cs == "iso-2022-jp" || cs == "csiso2022jp") return 50220;
    if (cs == "euc-kr" || cs == "cseuckr" || cs == "ks_c_5601-1987" || cs == "ks_c_5601-1989"
        || cs == "ksc_5601" || cs == "ksc5601" || cs == "korean"
        || cs == "cp949" || cs == "ms949" || cs == "windows-949" || cs == "uhc")
        return 949;
    if (cs == "utf-7" || cs == "unicode-1-1-utf-7") return 65000;
    if (cs == "windows-1250" || cs == "cp1250" || cs == "x-cp1250") return 1250;
    if (cs == "windows-1251" || cs == "cp1251" || cs == "x-cp1251") return 1251;
    if (cs == "windows-1252" || cs == "cp1252" || cs == "x-cp1252"
        || cs == "iso-8859-1" || cs == "iso_8859-1" || cs == "iso8859-1" || cs == "iso88591"
        || cs == "latin1" || cs == "latin-1" || cs == "l1" || cs == "cp819" || cs == "ibm819"
        || cs == "csisolatin1")
        return 1252;
    if (cs == "windows-1253" || cs == "cp1253") return 1253;
    if (cs == "windows-1254" || cs == "cp1254" || cs == "iso-8859-9" || cs == "latin5") return 1254;
    if (cs == "windows-1255" || cs == "cp1255") return 1255;
    if (cs == "windows-1256" || cs == "cp1256") return 1256;
    if (cs == "windows-1257" || cs == "cp1257") return 1257;
    if (cs == "windows-1258" || cs == "cp1258") return 1258;
    if (cs == "iso-8859-2" || cs == "latin2") return 28592;
    if (cs == "iso-8859-3" || cs == "latin3") return 28593;
    if (cs == "iso-8859-4" || cs == "latin4") return 28594;
    if (cs == "iso-8859-5") return 28595;
    if (cs == "iso-8859-6") return 28596;
    if (cs == "iso-8859-7") return 28597;
    if (cs == "iso-8859-8" || cs == "iso-8859-8-i") return 28598;
    if (cs == "iso-8859-13") return 28603;
    if (cs == "iso-8859-15" || cs == "latin9") return 28605;
    if (cs == "koi8-r" || cs == "cskoi8r") return 20866;
    if (cs == "koi8-u") return 21866;
    return 0;
}
#endif
bool tryDecodeChineseFallback(const QByteArray &data, QString &out) {
#ifdef Q_OS_WIN
    const UINT gb = IsValidCodePage(54936) ? 54936 : 936;
    if (tryDecodeCodePage(data, gb, true, out)) return true;
    if (gb != 936 && tryDecodeCodePage(data, 936, true, out)) return true;
    if (tryDecodeCodePage(data, 950, true, out)) return true;
#else
    for (const char *name : {"gb18030", "gbk", "big5"}) {
        QStringDecoder decoder(name, QStringConverter::Flag::Stateless);
        if (!decoder.isValid()) continue;
        QString decoded = decoder.decode(data);
        if (!decoder.hasError()) { out = std::move(decoded); return true; }
    }
#endif
    return false;
}
QString decodeUnlabeled(const QByteArray &data) {
    if (data.isEmpty()) return {};
    if (isPureAscii(data)) return QString::fromLatin1(data);
    QString out;
    if (tryDecodeUtf8(data, out)) return out;
    if (tryDecodeChineseFallback(data, out)) return out;
    QStringDecoder sys(QStringConverter::System, QStringConverter::Flag::Stateless);
    if (sys.isValid()) {
        QString decoded = sys.decode(data);
        if (!sys.hasError()) return decoded;
    }
    return QString::fromUtf8(data);
}
QByteArray canonicalCharsetGroup(const QByteArray &raw) {
    const QByteArray cs = normalizeCharset(raw);
    if (cs == "utf-8" || cs == "utf8" || cs == "unicode-1-1-utf-8") return "utf-8";
    if (cs == "gb18030" || cs == "gb-18030" || cs == "gb_18030"
        || cs == "gbk" || cs == "x-gbk" || cs == "cp936" || cs == "ms936" || cs == "windows-936"
        || cs == "gb2312" || cs == "gb-2312" || cs == "gb_2312" || cs == "gb_2312-80"
        || cs == "gb2312-80" || cs == "gb2312-1980" || cs == "csgb2312" || cs == "csiso58gb231280"
        || cs == "iso-ir-58" || cs == "chinese" || cs == "euc-cn" || cs == "euccn" || cs == "cn-gb")
        return "gb18030";
    if (cs == "big5" || cs == "big-5" || cs == "big5-hkscs" || cs == "big5hkscs"
        || cs == "cn-big5" || cs == "csbig5" || cs == "x-x-big5"
        || cs == "cp950" || cs == "ms950" || cs == "windows-950")
        return "big5";
    return cs;
}
QString text(const QByteArray &data, const QByteArray &charset) {
    if (data.isEmpty()) return {};
    const QByteArray cs = normalizeCharset(charset);
    if (cs.isEmpty() || cs == "unknown-8bit" || cs == "x-unknown" || cs == "default" || cs == "none"
        || cs == "us-ascii" || cs == "ascii" || cs == "ansi_x3.4-1968" || cs == "csascii"
        || cs == "cp367" || cs == "iso-ir-6")
        return decodeUnlabeled(data);

    QString out;
    if (cs == "utf-8" || cs == "utf8" || cs == "unicode-1-1-utf-8") {
        if (tryDecodeUtf8(data, out)) return out;
        if (tryDecodeChineseFallback(data, out)) return out;
        return QString::fromUtf8(data);
    }
    if (cs.startsWith("utf-16") || cs.startsWith("utf-32") || cs == "utf16" || cs == "utf32") {
        QStringDecoder decoder(cs.constData(), QStringConverter::Flag::Stateless);
        if (decoder.isValid()) return decoder.decode(data);
    }

#ifdef Q_OS_WIN
    if (const UINT cp = windowsCodePage(cs); cp != 0) {
        if (codePageSupportsStrict(cp)) {
            if (tryDecodeCodePage(data, cp, true, out)) return out;
            // Some mailers mislabel UTF-8 bytes as GBK/GB2312/Big5.
            if (cp != 1252 && tryDecodeUtf8(data, out)) return out;
        }
        if (tryDecodeCodePage(data, cp, false, out)) return out;
    }
#endif

    QStringDecoder decoder(cs.constData(), QStringConverter::Flag::Stateless);
    if (decoder.isValid()) {
        QString decoded = decoder.decode(data);
        if (!decoder.hasError()) return decoded;
        if (tryDecodeUtf8(data, out)) return out;
        return decoded;
    }
    return decodeUnlabeled(data);
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
QMap<QByteArray, QByteArray> parseParameters(const QByteArray &header) {
    QMap<QByteArray, QByteArray> params;
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    auto skipWsAndComments = [&](qsizetype &i) {
        while (i < header.size()) {
            if (isSpace(header[i])) { ++i; continue; }
            if (header[i] == '(') {
                int depth = 1; ++i;
                while (i < header.size() && depth > 0) {
                    if (header[i] == '\\' && i + 1 < header.size()) { i += 2; continue; }
                    if (header[i] == '(') ++depth;
                    else if (header[i] == ')') --depth;
                    ++i;
                }
                continue;
            }
            break;
        }
    };
    qsizetype i = header.indexOf(';');
    if (i < 0) return params;
    while (i < header.size()) {
        if (header[i] == ';') ++i;
        skipWsAndComments(i);
        if (i >= header.size()) break;
        const qsizetype nameStart = i;
        while (i < header.size() && header[i] != '=' && header[i] != ';' && !isSpace(header[i])) ++i;
        const QByteArray name = header.mid(nameStart, i - nameStart).trimmed().toLower();
        skipWsAndComments(i);
        if (i >= header.size() || header[i] != '=') {
            while (i < header.size() && header[i] != ';') ++i;
            continue;
        }
        ++i; // consume '='
        while (i < header.size() && isSpace(header[i])) ++i;
        QByteArray value;
        if (i < header.size() && header[i] == '"') {
            ++i; // consume opening '"'
            while (i < header.size()) {
                if (header[i] == '\\' && i + 1 < header.size()) {
                    value += header[i + 1]; i += 2;
                } else if (header[i] == '"') {
                    ++i; break;
                } else {
                    value += header[i++];
                }
            }
            // Skip any trailing whitespace or comments until next ';'.
            while (i < header.size() && header[i] != ';') ++i;
        } else {
            const qsizetype valStart = i;
            while (i < header.size() && header[i] != ';') ++i;
            value = header.mid(valStart, i - valStart).trimmed();
        }
        if (!name.isEmpty()) params[name] = value;
    }
    return params;
}
QString parameter(const QByteArray &header, const QByteArray &key, bool decode = true) {
    // RFC 2231 extended parameters, continuations, RFC 2047 encoded-words and raw bytes.
    const auto params = parseParameters(header);
    const QByteArray lowerKey = key.toLower();
    if (!decode) return QString::fromLatin1(params.value(lowerKey));

    auto decodeSingleExtended = [](const QByteArray &raw) -> QString {
        const auto a = raw.indexOf('\'');
        const auto b = a >= 0 ? raw.indexOf('\'', a + 1) : -1;
        if (a >= 0 && b > a) {
            const QByteArray charset = raw.left(a).trimmed();
            const QByteArray encoded = raw.mid(b + 1);
            return text(QByteArray::fromPercentEncoding(encoded), charset);
        }
        if (raw.contains("=?")) return decodeHeader(raw);
        return text(QByteArray::fromPercentEncoding(raw), {});
    };

    if (params.contains(lowerKey + "*")) {
        const QString value = decodeSingleExtended(params.value(lowerKey + "*"));
        if (!value.isEmpty()) return value;
    }

    const int start = (params.contains(lowerKey + "*0*") || params.contains(lowerKey + "*0")) ? 0
                    : (params.contains(lowerKey + "*1*") || params.contains(lowerKey + "*1")) ? 1 : -1;
    if (start >= 0) {
        QByteArray charset, rawBytes, encodedRun;
        bool anyEncoded = false, parsedCharset = false;
        auto flushEncoded = [&] {
            if (!encodedRun.isEmpty()) {
                rawBytes += QByteArray::fromPercentEncoding(encodedRun);
                encodedRun.clear();
            }
        };
        for (int i = start; i < 1000; ++i) {
            const QByteArray k = lowerKey + "*" + QByteArray::number(i);
            if (params.contains(k + "*")) {
                QByteArray chunk = params.value(k + "*");
                anyEncoded = true;
                if (!parsedCharset) {
                    parsedCharset = true;
                    const auto a = chunk.indexOf('\'');
                    const auto b = a >= 0 ? chunk.indexOf('\'', a + 1) : -1;
                    if (a >= 0 && b > a) {
                        charset = chunk.left(a).trimmed();
                        chunk = chunk.mid(b + 1);
                    }
                }
                encodedRun += chunk;
            } else if (params.contains(k)) {
                flushEncoded();
                rawBytes += params.value(k);
            } else {
                break;
            }
        }
        flushEncoded();
        QString value;
        if (!anyEncoded) value = decodeHeader(rawBytes);
        else if (charset.isEmpty() && rawBytes.contains("=?")) value = decodeHeader(rawBytes);
        else value = text(rawBytes, charset);
        if (!value.isEmpty()) return value;
    }

    if (params.contains(lowerKey)) return decodeHeader(params.value(lowerKey));
    return {};
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
    auto dispName = parameter(h.value("content-disposition"), "filename");
    auto typeName = parameter(h.value("content-type"), "name");
    auto isMangled = [](const QString &s) {
        return s.contains(QChar(0xFFFD)) || s.contains(QLatin1String("???"));
    };
    auto name = dispName;
    if (name.isEmpty() || (!typeName.isEmpty() && isMangled(name) && !isMangled(typeName)))
        name = typeName;
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
        auto boundary = parameter(h.value("content-type"), "boundary", false).toLatin1();
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
    // Scan on a 1:1 Latin-1 view so raw byte indices remain exact for unencoded GBK/UTF-8 text.
    const QString view = QString::fromLatin1(value);
    static const QRegularExpression re(R"(=\?([^?\s]+)\?([bBqQ])\?([^?]*)\?=)");
    auto it = re.globalMatch(view);
    QString out;
    QByteArray pendingBytes, pendingCharset, pendingGroup;
    auto flushPending = [&] {
        if (!pendingBytes.isEmpty()) {
            out += text(pendingBytes, pendingCharset);
            pendingBytes.clear();
            pendingCharset.clear();
            pendingGroup.clear();
        }
    };
    qsizetype pos = 0;
    bool previous = false;
    while (it.hasNext()) {
        const auto m = it.next();
        const QByteArray gap = value.mid(pos, m.capturedStart() - pos);
        const bool gapHasText = !gap.trimmed().isEmpty();
        if (!previous || gapHasText) {
            flushPending();
            out += decodeUnlabeled(gap);
        }
        const QByteArray rawCharset = m.captured(1).toLatin1();
        const QByteArray group = canonicalCharsetGroup(rawCharset);
        const QByteArray payload = m.captured(3).toLatin1();
        const QByteArray decoded = m.captured(2).toLower() == "b"
            ? QByteArray::fromBase64(payload) : unquotePrintable(payload, true);
        // Merge adjacent encoded-words with compatible charsets before decoding so
        // multi-byte UTF-8/GBK/GB18030 characters split across boundaries stay intact.
        if (!pendingBytes.isEmpty() && group == pendingGroup) {
            pendingBytes += decoded;
        } else {
            flushPending();
            pendingCharset = rawCharset;
            pendingGroup = group;
            pendingBytes = decoded;
        }
        pos = m.capturedEnd();
        previous = true;
    }
    flushPending();
    out += decodeUnlabeled(value.mid(pos));
    return out;
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
QString replyAddress(const Message &m, const QString &self, QString *skipReason) {
    if (skipReason) skipReason->clear();
    auto reject = [&](const QString &reason) { if (skipReason) *skipReason = reason; return QString(); };
    const auto &h = m.headers;
    if ((!h.value("auto-submitted").isEmpty() && h.value("auto-submitted").toLower() != "no")
        || h.contains("list-id") || h.contains("list-unsubscribe") || h.contains("x-auto-response-suppress")
        || h.value("return-path") == "<>" || h.contains("precedence"))
        return reject("自动邮件、列表邮件或自动回复抑制标记");
    QString from = QString::fromUtf8(h.value("from")).trimmed();
    static const QRegularExpression address(R"(^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    QString result = from;
    if (from.contains('<') || from.contains('>')) {
        // A mailbox's display name may itself be an email address. Only the
        // angle-addr is the actual mailbox: "sender@example.com" <sender@example.com>.
        if (from.count('<') != 1 || from.count('>') != 1 || !from.endsWith('>'))
            return reject("发件人字段不是单个邮箱");
        auto opening = from.indexOf('<');
        QString display = from.left(opening);
        bool quoted = false, escaped = false;
        for (QChar ch : display) {
            if (escaped) { escaped = false; continue; }
            if (quoted && ch == '\\') { escaped = true; continue; }
            if (ch == '"') { quoted = !quoted; continue; }
            if ((!quoted && (ch == ',' || ch == ';' || ch == ':' || ch == '>' || ch == '(' || ch == ')'))
                || ch == '\r' || ch == '\n') return reject("发件人字段不是单个邮箱");
        }
        if (quoted || escaped) return reject("发件人显示名称格式异常");
        result = from.mid(opening + 1, from.size() - opening - 2).trimmed();
    }
    if (!address.match(result).hasMatch()) return reject("无法识别单个有效发件人邮箱");
    if (result.compare(self.trimmed(), Qt::CaseInsensitive) == 0) return reject("自发邮件，避免自动回复循环");
    if (result.contains(QRegularExpression("no.?reply|mailer-daemon|postmaster", QRegularExpression::CaseInsensitiveOption)))
        return reject("发件人属于 no-reply 或系统邮箱");
    return result;
}
}
