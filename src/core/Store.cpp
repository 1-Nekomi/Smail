#include "smail/Store.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QFile>
#include <QTemporaryFile>
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
        "CREATE TABLE IF NOT EXISTS messages (id TEXT PRIMARY KEY, subject TEXT, status TEXT, detail TEXT, files TEXT, reply TEXT, done INTEGER, updated TEXT DEFAULT CURRENT_TIMESTAMP)",
        "CREATE TABLE IF NOT EXISTS single_files (message_id TEXT PRIMARY KEY, path TEXT NOT NULL UNIQUE COLLATE NOCASE)"}) {
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
QString Store::reserveSinglePath(const QString &id, const QString &root, const QString &name) {
    if (!db.transaction()) fail("无法开始附件路径事务");
    try {
        QSqlQuery q(db);
        q.prepare("SELECT path FROM single_files WHERE message_id=?");
        q.addBindValue(id); check(q);
        QString path;
        if (q.next()) path = q.value(0).toString();
        q.finish();
        if (path.isEmpty()) {
            QString filename = safeName(name);
            QFileInfo info(filename);
            QString suffix = info.suffix();
            QString stem = suffix.isEmpty() ? filename : filename.left(filename.size()-suffix.size()-1);
            QDir dir(root);
            for (int index = 1; ; ++index) {
                QString candidate = index == 1 ? filename : stem + QString(" (%1)").arg(index)
                    + (suffix.isEmpty() ? QString() : "." + suffix);
                path = dir.absoluteFilePath(candidate);
                q.prepare("SELECT 1 FROM single_files WHERE path=? COLLATE NOCASE");
                q.addBindValue(path); check(q);
                bool reserved = q.next(); q.finish();
                if (!reserved && !QFileInfo::exists(path) && !QFileInfo(path).isSymLink()) break;
            }
            q.prepare("INSERT INTO single_files (message_id,path) VALUES (?,?)");
            q.addBindValue(id); q.addBindValue(path); check(q); q.finish();
        }
        if (!db.commit()) fail("无法保存附件路径");
        return path;
    } catch (...) { db.rollback(); throw; }
}
QStringList saveAttachments(const QString &root, const QString &id, const QVector<Attachment> &attachments, Store &store) {
    if (id.isEmpty() || id.contains('/') || id.contains('\\') || id.contains("..")) fail("消息目录标识无效");
    if (attachments.isEmpty()) return {};
    QDir dir(root);
    if (attachments.size() == 1) {
        if (!dir.mkpath(".")) fail("无法创建附件目录");
        const auto &a = attachments.first();
        // Reserve before writing so a crash before the message record is saved
        // reuses exactly the same filename on the next attempt.
        QString path = store.reserveSinglePath(id, root, a.name);
        if (QFileInfo(path).isSymLink()) fail("附件路径不可为符号链接");
        if (QFileInfo::exists(path)) {
            QFile existing(path);
            if (!existing.open(QIODevice::ReadOnly) || existing.size() != a.data.size() || existing.readAll() != a.data)
                fail("已预留附件路径被其他文件占用，请核查：" + path);
            return {path};
        }
        QTemporaryFile f(QFileInfo(path).absolutePath() + "/.smail-XXXXXX");
        if (!f.open() || f.write(a.data) != a.data.size() || !f.flush()) fail("附件写入失败：" + path);
        f.close();
        // QFile::rename refuses to overwrite a destination created concurrently.
        if (!f.rename(path)) fail("附件提交失败（目标可能已被占用）：" + path + "：" + f.errorString());
        f.setAutoRemove(false);
        return {path};
    }
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
