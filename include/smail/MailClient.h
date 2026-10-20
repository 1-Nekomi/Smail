#pragma once
#include "smail/Config.h"
#include <QSslSocket>
#include <functional>
#include <QVector>

namespace smail {
struct ImapResponse { QByteArray lines; QList<QByteArray> literals; };
class MailClient {
public:
    explicit MailClient(std::function<bool()> cancelled = {});
    void login(const Config &config);
    QString selectInbox();
    QVector<quint64> search(quint64 after);
    qint64 messageSize(quint64 uid);
    QByteArray fetch(quint64 uid, qint64 limit);
    void markRead(quint64 uid);
    static void sendReply(const Config &config, const QString &to, const QString &body,
                          std::function<bool()> cancelled = {});
    static QVector<quint64> parseSearch(const QByteArray &lines, quint64 after);
private:
    QSslSocket socket;
    std::function<bool()> cancelled;
    int serial = 0;
    void connectTls(const QString &host, int port);
    void write(const QByteArray &data);
    QByteArray line();
    QByteArray bytes(qint64 size);
    void checkCancelled();
    ImapResponse command(const QByteArray &command, qint64 limit = 1024*1024);
    int smtpResponse();
    void smtp(const QByteArray &command, int expected);
};
}
