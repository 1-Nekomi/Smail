// Explicit read-only diagnostic. Never downloads message bodies or sends mail.
#include "smail/Config.h"
#include "smail/CredentialStore.h"
#include "smail/MailClient.h"
#include "smail/Mime.h"
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTextStream output(stdout);
    if (app.arguments().size()!=2) return 2;
    try {
        QString root=app.arguments()[1];
        auto c=smail::Config::load(root+"/settings.json");
        c.secret=smail::CredentialStore(root+"/credentials").load(c);
        if (c.secret.isEmpty()) smail::fail("没有已保存授权码，无法进行只读检查");
        QSslSocket::setActiveBackend("schannel");
        auto db=QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName(root+"/history.sqlite"); db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!db.open()) smail::fail("无法只读打开历史数据库");
        QSqlQuery q(db);
        if (!q.exec("SELECT id FROM messages WHERE reply='none' AND status IN ('saved','ignored') ORDER BY updated DESC LIMIT 20")) smail::fail("无法读取诊断记录");
        QStringList ids; while (q.next()) ids << q.value(0).toString();
        smail::MailClient client; client.login(c); auto validity=client.selectInbox();
        for (const auto &id:ids) {
            QString prefix=c.accountKey()+"-"+validity+"-";
            if (!id.startsWith(prefix)) continue;
            auto uid=id.mid(prefix.size()).toULongLong();
            QByteArray raw;
            try { raw=client.fetchReplyHeaders(uid); }
            catch (const std::exception &) { output << "Header unavailable for UID " << uid << Qt::endl; continue; }
            raw += "\r\n\r\n";
            auto m=smail::parseMessage(raw);
            QString reason;
            auto recipient=smail::replyAddress(m,c.email,&reason);
            QJsonObject result{{"uid",QString::number(uid)},
                {"reply_address_accepted",!recipient.isEmpty()}, {"skip_reason",reason}};
            auto from=QString::fromUtf8(m.headers.value("from"));
            result["from"]=from;
            result["from_has_angle_brackets"]=from.contains('<');
            result["from_ends_angle_bracket"]=from.trimmed().endsWith('>');
            result["from_at_signs"]=from.count('@');
            result["from_contains_self"]=from.contains(c.email,Qt::CaseInsensitive);
            for (auto key:{"auto-submitted","list-id","list-unsubscribe","x-auto-response-suppress","precedence"}) {
                result[QString::fromLatin1(key)+"_present"]=m.headers.contains(key);
                if (QByteArray(key)!="list-id" && QByteArray(key)!="list-unsubscribe")
                    result[QString::fromLatin1(key)]=QString::fromUtf8(m.headers.value(key)).left(100);
            }
            output << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
        }
        return 0;
    } catch(const std::exception &e) { output << e.what() << Qt::endl; return 1; }
}
