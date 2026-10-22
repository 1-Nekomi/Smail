#pragma once
#include "smail/Mime.h"
#include <QSqlDatabase>

namespace smail {
struct Record {
    QString id, subject, status, detail, replyState = "none";
    QStringList files;
    bool exists = false, done = false;
};
class Store {
public:
    explicit Store(const QString &path);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    QString checkpoint(const QString &key);
    void checkpoint(const QString &key, const QString &value);
    QVector<quint64> pendingUnread(const QString &key);
    void queueUnread(const QString &key, const QVector<quint64> &uids);
    void completeUid(const QString &key, quint64 uid);
    Record record(const QString &id);
    void put(const Record &record);
    QString reserveSinglePath(const QString &messageId, const QString &root, const QString &name);
    QStringList history(int limit = 100);
private:
    QString connection;
    QSqlDatabase db;
};
QStringList saveAttachments(const QString &root, const QString &messageId, const QVector<Attachment> &attachments, Store &store);
}
