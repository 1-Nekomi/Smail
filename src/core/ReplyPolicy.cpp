#include "smail/ReplyPolicy.h"
#include <QRegularExpression>

namespace smail {
ReplyPlan planReply(const Message &message, const Selection &selection,
                    qsizetype savedCount, const Config &config) {
    QString content;
    if (selection.status == "rejected") {
        if (!config.reply) return {{}, {}, "拒收回复未启用"};
        content = config.rejection;
    } else if (selection.status == "saved" && !message.attachments.isEmpty()
               && savedCount > 0 && savedCount == selection.accepted.size()) {
        if (!config.withAttachmentReply) return {{}, {}, "有附件回复未启用"};
        content = config.withAttachmentMessage;
    } else if (selection.status == "ignored" && message.attachments.isEmpty() && savedCount == 0) {
        if (!config.withoutAttachmentReply) return {{}, {}, "无附件回复未启用"};
        content = config.withoutAttachmentMessage;
    } else return {{}, {}, "附件未完整保存或邮件处理状态不符合回复条件"};
    if (content.trimmed().isEmpty()) return {{}, {}, "回复文字为空"};
    QString skipReason;
    QString recipient = replyAddress(message, config.email, &skipReason);
    if (recipient.isEmpty()) return {{}, {}, skipReason};
    // Expand only tokens in the original template, not recursively in inserted text.
    static const QRegularExpression token("\\{(reason|allowed_types|attachment_count)\\}");
    auto matches = token.globalMatch(content);
    QString body; qsizetype position = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        body += content.mid(position, match.capturedStart() - position);
        const QString key = match.captured(1);
        if (key == "reason") body += selection.reason;
        else if (key == "attachment_count") body += QString::number(savedCount);
        else body += config.extensions.isEmpty() ? "不限扩展名" : config.extensions.join(", ");
        position = match.capturedEnd();
    }
    body += content.mid(position);
    return {recipient, body, {}};
}
}
