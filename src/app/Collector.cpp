#include "smail/Collector.h"
#include "smail/MailClient.h"
#include "smail/Store.h"
#include "smail/ReplyPolicy.h"
#include "smail/ReplyMessage.h"
#include <algorithm>

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
        emit activity(config.includeExisting ? "首次连接，将收取历史邮件" : "新邮件基线已建立，将补查启动前的未读邮件");
    }
    if (scannedMailbox != key) {
        auto unread = client.searchUnread();
        // Persist the snapshot so interruption or another client's read flag cannot lose work.
        store.queueUnread(key, unread);
        scannedMailbox = key;
        emit activity(QString("启动未读检查：%1 封，将按当前收取和回复规则处理（已处理记录自动去重）").arg(unread.size()));
    }
    auto uids = client.search(last.toULongLong());
    uids += store.pendingUnread(key);
    std::sort(uids.begin(), uids.end());
    uids.erase(std::unique(uids.begin(), uids.end()), uids.end());
    for (auto uid : uids) {
        if (isInterruptionRequested()) return;
        QString id = key + "-" + QString::number(uid);
        auto r = store.record(id);
        if (!r.exists) {
            qint64 size = client.messageSize(uid);
            if (size < 0) { store.completeUid(key, uid); continue; }
            r.subject = QString("UID %1").arg(uid);
            ReplyPlan response;
            if (size > qint64(config.maxMessageMb)*1024*1024) {
                r.status = "rejected"; r.detail = "附件状态未知 · 整封邮件超限，未下载、不自动回复";
            } else {
                auto message = parseMessage(client.fetch(uid, qint64(config.maxMessageMb)*1024*1024), config.inlineImages);
                r.subject = decodeHeader(message.headers.value("subject"));
                auto selection = selectAttachments(message, config);
                r.status = selection.status;
                r.detail = (message.attachments.isEmpty() ? QStringLiteral("无附件 · ") : QStringLiteral("有附件 · ")) + selection.reason;
                if (!selection.accepted.isEmpty()) r.files = saveAttachments(config.outputDir, id, selection.accepted, store);
                response = planReply(message, selection, r.files.size(), config);
                if (!response.skipReason.isEmpty()) r.detail += "；未回复：" + response.skipReason;
            }
            // Persist the result before any external side effect. A crash can suppress a
            // reply, but will not cause duplicate automatic replies.
            // A missing/unreadable reply file is retryable, not an uncertain SMTP delivery.
            QByteArray outgoing;
            if (!response.recipient.isEmpty())
                outgoing = composeReply(config.email, response.recipient, response.subject, response.body, response.files, response.images);
            r.replyState = response.recipient.isEmpty() ? "none" : "attempted";
            store.put(r);
            if (!response.recipient.isEmpty()) {
                try {
                    MailClient::sendPreparedReply(config, response.recipient, outgoing, [this] { return isInterruptionRequested(); });
                    r.replyState = "sent";
                } catch (const std::exception &e) {
                    r.replyState = "uncertain";
                    emit activity("自动回复未确认，已记录 uncertain；请人工核查，不会自动重发");
                    emit activity("SMTP 详情：" + QString::fromUtf8(e.what()));
                    emit activity("可点击“测试 SMTP 发信连接”检查网络或代理是否允许 SMTP。");
                }
                store.put(r);
            }
        }
        if (!r.done) {
            client.markRead(uid);
            r.done = true; store.put(r);
            emit processed(r.subject, r.detail + QString(" [回复：%1]").arg(r.replyState));
        }
        store.completeUid(key, uid);
    }
    emit activity(QString("本轮检查完成：%1 封邮件（含启动时未读邮件，已处理记录自动去重）").arg(uids.size()));
}
}
