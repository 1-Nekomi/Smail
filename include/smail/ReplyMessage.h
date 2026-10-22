#pragma once
#include "smail/Config.h"

namespace smail {
// Outgoing limits are independent of incoming attachment filters.
constexpr qint64 ReplyFilesLimit = 25LL * 1024 * 1024;
QString validateReplyFiles(const QStringList &paths);
QString validateReplySubject(const QString &subject);
// Reads and validates every file before the caller records a send attempt.
QByteArray composeReply(const QString &from, const QString &to, const QString &subject,
                        const QString &body, const QStringList &paths = {});
}
