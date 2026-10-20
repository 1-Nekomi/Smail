#include "smail/Store.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QVariant>

namespace smail {
namespace {
void check(QSqlQuery &q) { if (!q.exec()) fail("数据库操作失败：" + q.lastError().text()); }
}
Store::Store(const QString &path) {
    connection = QUuid::createUuid().toString();
    db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(path);
    if (!db.open()) fail("无法打开 SQLite：" + db.lastError().text());
    QSqlQuery q(db);
    for (const auto &sql : {
        "PRAGMA journal_mode=WAL",
        "CREATE TABLE IF NOT EXISTS checkpoint (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS messages (id TEXT PRIMARY KEY, subject TEXT, status TEXT, detail TEXT, files TEXT, reply TEXT, done INTEGER, updated TEXT DEFAULT CURRENT_TIMESTAMP)"}) {
        q.prepare(sql); check(q);
    }
}
Store::~Store() { db.close(); db = {}; QSqlDatabase::removeDatabase(connection); }
QString Store::checkpoint(const QString &key) {
    QSqlQuery q(db); q.prepare("SELECT value FROM checkpoint WHERE key=?"); q.addBindValue(key); check(q);
    return q.next() ? q.value(0).toString() : QString();
}
void Store::checkpoint(const QString &key, const QString &value) {
    QSqlQuery q(db); q.prepare("INSERT OR REPLACE INTO checkpoint VALUES (?,?)"); q.addBindValue(key); q.addBindValue(value); check(q);
}
Record Store::record(const QString &id) {
    Record r; r.id = id;
    QSqlQuery q(db); q.prepare("SELECT subject,status,detail,files,reply,done FROM messages WHERE id=?");
    q.addBindValue(id); check(q);
    if (q.next()) {
        r.exists = true; r.subject = q.value(0).toString(); r.status = q.value(1).toString();
        r.detail = q.value(2).toString(); r.replyState = q.value(4).toString(); r.done = q.value(5).toBool();
        for (auto v : QJsonDocument::fromJson(q.value(3).toByteArray()).array()) r.files << v.toString();
    }
    return r;
}
void Store::put(const Record &r) {
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO messages (id,subject,status,detail,files,reply,done,updated) VALUES (?,?,?,?,?,?,?,CURRENT_TIMESTAMP)");
    q.addBindValue(r.id); q.addBindValue(r.subject); q.addBindValue(r.status); q.addBindValue(r.detail);
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(r.files)).toJson(QJsonDocument::Compact)));
    q.addBindValue(r.replyState); q.addBindValue(r.done); check(q);
}
QStringList Store::history(int limit) {
    QStringList out; QSqlQuery q(db);
    q.prepare("SELECT updated,subject,status,detail,reply,done,files FROM messages ORDER BY updated DESC LIMIT ?");
    q.addBindValue(limit); check(q);
    while (q.next()) out << QString("%1 UTC | %2 | %3 | %4 | reply=%5 | %6\n%7")
        .arg(q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
             q.value(4).toString(), q.value(5).toBool() ? "完成" : "待标记已读", q.value(6).toString());
    return out;
}
QStringList saveAttachments(const QString &root, const QString &id, const QVector<Attachment> &attachments) {
    if (id.isEmpty() || id.contains('/') || id.contains('\\') || id.contains("..")) fail("消息目录标识无效");
    QDir dir(root);
    if (!dir.mkpath(id)) fail("无法创建附件目录");
    QString base = dir.absoluteFilePath(id);
    if (QFileInfo(base).isSymLink()) fail("附件子目录不可为符号链接");
    QStringList paths; int index = 0;
    for (const auto &a : attachments) {
        QString path = QDir(base).absoluteFilePath(QString::number(++index) + "_" + safeName(a.name));
        if (QFileInfo(path).isSymLink()) fail("附件路径不可为符号链接");
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(a.data) != a.data.size() || !f.commit()) fail("附件写入失败：" + path);
        paths << path;
    }
    return paths;
}
}
