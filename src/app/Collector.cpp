#include "smail/Collector.h"
#include "smail/MailClient.h"
#include "smail/Store.h"

namespace smail {
Collector::Collector(Config c, QString path, QObject *parent)
    : QThread(parent), config(std::move(c)), statePath(std::move(path)) {}
void Collector::run() {
    int failures = 0;
    while (!isInterruptionRequested()) {
        try { poll(); failures = 0; }
        catch (const std::exception &e) { if (!isInterruptionRequested()) { emit activity(QString::fromUtf8(e.what())); ++failures; } }
        const int delay = qMin(900, config.interval * (1 << qMin(failures, 5)));
        if (!isInterruptionRequested()) emit activity(QString("下次检查：%1 秒后").arg(delay));
        for (int i = 0; i < delay * 10 && !isInterruptionRequested(); ++i) msleep(100);
    }
    config.secret.clear();
}
void Collector::poll() {
    Store store(statePath);
    MailClient client([this] { return isInterruptionRequested(); });
    emit activity("正在连接邮箱…");
    client.login(config);
    QString validity = client.selectInbox(), key = config.accountKey() + "-" + validity;
    QString last = store.checkpoint(key);
    if (last.isNull()) {
        auto all = client.search(0);
        quint64 baseline = config.includeExisting || all.isEmpty() ? 0 : all.last();
        store.checkpoint(key, QString::number(baseline));
        last = QString::number(baseline);
        emit activity(config.includeExisting ? "首次连接，将收取历史邮件" : "新邮件基线已建立，请发送测试邮件");
    }
    auto uids = client.search(last.toULongLong());
    for (auto uid : uids) {
        if (isInterruptionRequested()) return;
        QString id = key + "-" + QString::number(uid);
        auto r = store.record(id);
        if (!r.exists) {
            qint64 size = client.messageSize(uid);
            if (size < 0) { store.checkpoint(key, QString::number(uid)); continue; }
            r.subject = QString("UID %1").arg(uid);
            QString recipient;
            if (size > qint64(config.maxMessageMb)*1024*1024) {
                r.status = "rejected"; r.detail = "整封邮件超限，未下载、不自动回复";
            } else {
                auto message = parseMessage(client.fetch(uid, qint64(config.maxMessageMb)*1024*1024), config.inlineImages);
                r.subject = decodeHeader(message.headers.value("subject"));
                auto selection = selectAttachments(message, config);
                r.status = selection.status; r.detail = selection.reason;
                if (!selection.accepted.isEmpty()) r.files = saveAttachments(config.outputDir, id, selection.accepted, store);
                if (r.status == "rejected" && config.reply) recipient = replyAddress(message, config.email);
            }
            // Persist the result before any external side effect. A crash can suppress a
            // reply, but will not cause duplicate automatic replies.
            r.replyState = recipient.isEmpty() ? "none" : "attempted";
            store.put(r);
            if (!recipient.isEmpty()) {
                QString body = config.rejection;
                body.replace("{allowed_types}", config.extensions.isEmpty() ? "不限扩展名" : config.extensions.join(", "));
                body.replace("{reason}", r.detail);
                try {
                    MailClient::sendReply(config, recipient, body, [this] { return isInterruptionRequested(); });
                    r.replyState = "sent";
                } catch (const std::exception &) {
                    r.replyState = "uncertain";
                    emit activity("拒收回复未确认，已记录 uncertain；请人工核查，不会自动重发");
                }
                store.put(r);
            }
        }
        if (!r.done) {
            client.markRead(uid);
            r.done = true; store.put(r);
            emit processed(r.subject, r.detail + QString(" [回复：%1]").arg(r.replyState));
        }
        store.checkpoint(key, QString::number(uid));
    }
    emit activity(QString("本轮检查完成：%1 封新邮件").arg(uids.size()));
}
}
