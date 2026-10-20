#pragma once
#include "smail/Config.h"
#include <QMap>
#include <QVector>

namespace smail {
struct Attachment { QString name, mime; QByteArray data; };
struct Message { QMap<QByteArray, QByteArray> headers; QVector<Attachment> attachments; };
struct Selection { QVector<Attachment> accepted; QString status, reason; };
Message parseMessage(const QByteArray &raw, bool includeInline = false);
Selection selectAttachments(const Message &message, const Config &config);
QString safeName(QString name);
QString replyAddress(const Message &message, const QString &self);
QString decodeHeader(const QByteArray &value);
}
