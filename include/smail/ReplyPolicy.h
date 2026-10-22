#pragma once
#include "smail/Mime.h"

namespace smail {
struct ReplyPlan {
    QString recipient, body, skipReason;
    QString subject;
    QStringList files;
};
// Call after saving attachments; savedCount is the number actually written.
ReplyPlan planReply(const Message &message, const Selection &selection,
                    qsizetype savedCount, const Config &config);
}
