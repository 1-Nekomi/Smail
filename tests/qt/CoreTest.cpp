#include "smail/Config.h"
#include "smail/Mime.h"
#include "smail/Store.h"
#include "smail/MailClient.h"
#include "smail/CredentialStore.h"
#include "smail/ReplyPolicy.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

using namespace smail;
class CoreTest : public QObject {
    Q_OBJECT
private slots:
    void startupUnreadSurvivesRestartAndKeepsCursor() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/unread.sqlite";
        {
            Store store(path);
            store.checkpoint("box", "100");
            store.queueUnread("box", {9, 3, 9, 105});
            store.completeUid("box", 3);
            QCOMPARE(store.checkpoint("box"), QString("100"));
        }
        Store store(path);
        QCOMPARE(store.pendingUnread("box"), QVector<quint64>({9, 105}));
        store.queueUnread("box", {9, 20});
        QCOMPARE(store.pendingUnread("box"), QVector<quint64>({9, 20, 105}));
        QVERIFY(store.pendingUnread("other-validity").isEmpty());
        store.completeUid("box", 9); // Also used when an old message was deleted.
        store.completeUid("box", 20);
        store.completeUid("box", 105);
        QCOMPARE(store.checkpoint("box"), QString("105"));
        QVERIFY(store.pendingUnread("box").isEmpty());
    }
    void mimeAndRules() {
        auto m = parseMessage("Received: by relay2\r\nReceived: by relay1\r\nFrom: Test <sender@example.com>\r\nSubject: test\r\nContent-Type: multipart/mixed; boundary=\"b\"\r\n\r\n"
            "--b\r\nContent-Type: text/plain\r\n\r\nhello\r\n"
            "--b\r\nContent-Type: application/pdf\r\nContent-Disposition: attachment; filename=\"REPORT.PDF\"\r\nContent-Transfer-Encoding: base64\r\n\r\nJVBERg==\r\n"
            "--b\r\nContent-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=bad.exe\r\n\r\nbad\r\n--b--\r\n");
        QCOMPARE(m.attachments.size(),2); QCOMPARE(m.attachments[0].data,QByteArray("%PDF"));
        Config c; auto s = selectAttachments(m,c); QCOMPARE(s.status,QString("saved")); QCOMPARE(s.accepted.size(),1);
        c.strict = true; QCOMPARE(selectAttachments(m,c).status,QString("rejected"));
        c.strict = false; c.mimeTypes = {"image/*"}; QCOMPARE(selectAttachments(m,c).status,QString("rejected"));
        QCOMPARE(replyAddress(m,"me@example.com"),QString("sender@example.com"));
        m.headers["auto-submitted"] = "auto-replied"; QVERIFY(replyAddress(m,"me@example.com").isEmpty());
    }
    void filenamesAndEncoding() {
        auto m = parseMessage("Content-Type: application/pdf\r\nContent-Disposition: attachment;\r\n filename*0*=UTF-8''%E6%B5%8B; filename*1*=%E8%AF%95.pdf\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nA=00B=\r\nC");
        QCOMPARE(m.attachments[0].name,QString::fromUtf8("测试.pdf"));
        QCOMPARE(m.attachments[0].data,QByteArray("A\0BC",4));
        QCOMPARE(safeName("../a\\b.pdf"),QString(".._a_b.pdf"));
        QCOMPARE(safeName("CON.txt"),QString("_CON.txt"));
        QCOMPARE(decodeHeader("=?UTF-8?B?5rWL6K+V?="),QString::fromUtf8("测试"));
    }
    void inlineAndInvalid() {
        QByteArray raw = "Content-Type: image/png\r\nContent-Disposition: inline; filename=x.png\r\n\r\npng";
        QVERIFY(parseMessage(raw).attachments.isEmpty()); QCOMPARE(parseMessage(raw,true).attachments.size(),1);
        QVERIFY_EXCEPTION_THROWN(parseMessage("Content-Type: multipart/mixed; boundary=b\r\n\r\n--b\r\n"),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(parseMessage("Content-Disposition: attachment; filename=x.pdf\r\nContent-Transfer-Encoding: base64\r\n\r\n!!!!"),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(parseMessage("From: a@example.com\r\nFrom: b@example.com\r\n\r\nbody"),std::runtime_error);
    }
    void storageAndRecovery() {
        QTemporaryDir temp;
        Store files(temp.path()+"/files.sqlite");
        QVector<Attachment> attachments = {{"../a.pdf","application/pdf","abc"},{"../a.pdf","application/pdf","def"}};
        QStringList paths = saveAttachments(temp.path(),"message",attachments,files);
        QCOMPARE(paths.size(),2); QVERIFY(paths[0] != paths[1]);
        QCOMPARE(saveAttachments(temp.path(),"message",attachments,files),paths);
        QFile f(paths.first()); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),QByteArray("abc"));
        QVERIFY_EXCEPTION_THROWN(saveAttachments(temp.path(),"../escape",{},files),std::runtime_error);
        {
            Store store(temp.path()+"/test.sqlite"); QVERIFY(store.checkpoint("key").isNull()); store.checkpoint("key","0");
            Record r; r.id="one"; r.status="saved"; r.files=paths; r.replyState="attempted"; store.put(r);
        }
        Store store(temp.path()+"/test.sqlite"); QCOMPARE(store.checkpoint("key"),QString("0"));
        auto r=store.record("one"); QVERIFY(r.exists); QVERIFY(!r.done); QCOMPARE(r.files,paths);
        QCOMPARE(r.replyState,QString("attempted")); r.done=true; store.put(r); QVERIFY(store.record("one").done);
    }
    void singleAttachmentFlatAndRetry() {
        QTemporaryDir temp;
        QString output = temp.path()+"/downloads";
        QString first;
        {
            Store store(temp.path()+"/state.sqlite");
            first = saveAttachments(output,"first",{{"报告.pdf","application/pdf","one"}},store).first();
            QCOMPARE(first,QDir(output).absoluteFilePath("报告.pdf"));
            QCOMPARE(saveAttachments(output,"second",{{"报告.pdf","application/pdf","two"}},store).first(),
                     QDir(output).absoluteFilePath("报告 (2).pdf"));
            QVERIFY(QDir(output).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
        }
        Store reopened(temp.path()+"/state.sqlite");
        QCOMPARE(saveAttachments(output,"first",{{"报告.pdf","application/pdf","one"}},reopened).first(),first);
        QCOMPARE(QDir(output).entryList(QDir::Files).size(),2);
        QFile external(QDir(output).filePath("existing.pdf"));
        QVERIFY(external.open(QIODevice::WriteOnly)); external.write("user file"); external.close();
        auto path = saveAttachments(output,"third",{{"existing.pdf","application/pdf","mail"}},reopened).first();
        QCOMPARE(QFileInfo(path).fileName(),QString("existing (2).pdf"));
        QVERIFY(external.open(QIODevice::ReadOnly)); QCOMPARE(external.readAll(),QByteArray("user file"));
        QVERIFY_EXCEPTION_THROWN(saveAttachments(output,"first",{{"报告.pdf","application/pdf","changed"}},reopened),std::runtime_error);
    }
    void configNeverPersistsSecret() {
        QTemporaryDir temp; Config c; c.email="me@qq.com"; c.secret="sensitive"; c.outputDir=temp.path();
        QVERIFY(c.validate().isEmpty()); c.save(temp.path()+"/config.json");
        QFile f(temp.path()+"/config.json"); QVERIFY(f.open(QIODevice::ReadOnly)); QVERIFY(!f.readAll().contains("sensitive"));
        auto loaded=Config::load(temp.path()+"/config.json"); QVERIFY(loaded.secret.isEmpty()); QCOMPARE(loaded.email,c.email);
        c.maxMessageMb=1; QVERIFY(!c.validate().isEmpty());
    }
    void encryptedCredentialsPersistAndForget() {
        try {
        if (!CredentialStore::supported()) QSKIP("DPAPI requires Windows");
        QTemporaryDir temp;
        Config c; c.email="test@qq.com"; c.secret="test-only-credential-测试";
        {
            CredentialStore store(temp.path());
            QVERIFY(store.load(c).isEmpty());
            store.save(c);
        }
        CredentialStore reopened(temp.path());
        QCOMPARE(reopened.load(c),c.secret);
        const auto files = QDir(temp.path()).entryList({"*.dpapi"},QDir::Files);
        QCOMPARE(files.size(),1);
        QFile blob(QDir(temp.path()).filePath(files.first()));
        QVERIFY(blob.open(QIODevice::ReadOnly));
        QVERIFY(!blob.readAll().contains(c.secret.toUtf8())); blob.close();
        auto changed = c; changed.smtpHost="another.example.com";
        QVERIFY(reopened.load(changed).isEmpty());
        c.secret="replacement-test-only";
        reopened.save(c); QCOMPARE(reopened.load(c),c.secret);
        reopened.remove(c); QVERIFY(reopened.load(c).isEmpty());
        reopened.remove(c); // Forget is idempotent.
        } catch (const std::exception &e) { QFAIL(e.what()); }
    }
    void encryptedCredentialsRejectTamperingAndWrongScope() {
        try {
        if (!CredentialStore::supported()) QSKIP("DPAPI requires Windows");
        QTemporaryDir temp;
        CredentialStore store(temp.path());
        Config c; c.email="first@qq.com"; c.secret="first-test";
        store.save(c);
        QString first = QDir(temp.path()).entryList({"*.dpapi"},QDir::Files).first();
        QFile original(QDir(temp.path()).filePath(first));
        QVERIFY(original.open(QIODevice::ReadOnly)); QByteArray data=original.readAll(); original.close();
        Config other=c; other.email="second@qq.com"; other.secret="second-test"; store.save(other);
        auto names=QDir(temp.path()).entryList({"*.dpapi"},QDir::Files); names.removeAll(first);
        QCOMPARE(names.size(),1);
        QFile swapped(QDir(temp.path()).filePath(names.first()));
        QVERIFY(swapped.open(QIODevice::WriteOnly)); QCOMPARE(swapped.write(data),data.size()); swapped.close();
        QVERIFY_EXCEPTION_THROWN(store.load(other),std::runtime_error);
        QVERIFY(original.open(QIODevice::WriteOnly)); original.write("corrupted"); original.close();
        QVERIFY_EXCEPTION_THROWN(store.load(c),std::runtime_error);
        } catch (const std::exception &e) { QFAIL(e.what()); }
    }
    void searchSemantics() {
        QCOMPARE(MailClient::parseSearch("* SEARCH 8 1 4 8\r\n",3),QVector<quint64>({4,8}));
        QVERIFY(MailClient::parseSearch("* SEARCH 8\r\n",9).isEmpty());
        QVERIFY_EXCEPTION_THROWN(MailClient::parseSearch("* SEARCH nope\r\n",0),std::runtime_error);
    }
    void noReplyLoops() {
        Message m; m.headers["from"]="a@example.com, b@example.com"; QVERIFY(replyAddress(m,"me@example.com").isEmpty());
        m.headers["from"]="me@example.com"; QVERIFY(replyAddress(m,"me@example.com").isEmpty());
        m.headers["from"]="no-reply@example.com"; QVERIFY(replyAddress(m,"me@example.com").isEmpty());
        m.headers["from"]="a@example.com"; m.headers["list-id"]="list"; QVERIFY(replyAddress(m,"me@example.com").isEmpty());
    }
    void displayNameEmailIsNotAnotherSender() {
        Config c; c.email="collector@example.com"; c.withoutAttachmentReply=true; c.withAttachmentReply=true;
        Message m; m.headers["from"]="\"sender@example.com\" <sender@example.com>";
        QCOMPARE(replyAddress(m,c.email),QString("sender@example.com"));
        QCOMPARE(planReply(m,selectAttachments(m,c),0,c).recipient,QString("sender@example.com"));
        m.attachments={{"a.pdf","application/pdf","pdf"}};
        QCOMPARE(planReply(m,selectAttachments(m,c),1,c).recipient,QString("sender@example.com"));
        m.headers["from"]="sender@example.com <sender@example.com>";
        QCOMPARE(replyAddress(m,c.email),QString("sender@example.com"));
        m.headers["from"]="\"alias@example.com, support\" <sender@example.com>";
        QCOMPARE(replyAddress(m,c.email),QString("sender@example.com"));
        m.headers["from"]="a@example.com, Other <b@example.com>";
        QVERIFY(replyAddress(m,c.email).isEmpty());
        m.headers["from"]="<a@example.com>, <b@example.com>";
        QVERIFY(replyAddress(m,c.email).isEmpty());
        m.headers["from"]="\"sender@example.com\" <collector@example.com>";
        auto plan=planReply(m,selectAttachments(m,c),1,c);
        QVERIFY(plan.recipient.isEmpty()); QVERIFY(plan.skipReason.contains("自发邮件"));
        // Formats observed in real QQ/163/Gmail mailboxes.
        m.headers["from"]="\"nekomi.typedef@gmail.com\" <nekomi.typedef@gmail.com>";
        QCOMPARE(replyAddress(m,c.email),QString("nekomi.typedef@gmail.com"));
        m.headers["from"]="\"=?utf-8?B?5oCd?=\" <1502410295@qq.com>";
        QCOMPARE(replyAddress(m,c.email),QString("1502410295@qq.com"));
        m.headers["from"]="=?GBK?B?wu27osH6?= <18683423521@163.com>";
        QCOMPARE(replyAddress(m,c.email),QString("18683423521@163.com"));
    }
    void skippedRepliesExplainWhy() {
        Config c; c.email="collector@example.com";
        Message m; m.headers["from"]="sender@example.com";
        auto selection=selectAttachments(m,c);
        auto plan=planReply(m,selection,0,c);
        QVERIFY(plan.recipient.isEmpty()); QCOMPARE(plan.skipReason,QString("无附件回复未启用"));
        c.withoutAttachmentReply=true; c.withoutAttachmentMessage="请补发附件";
        plan=planReply(m,selection,0,c);
        QCOMPARE(plan.recipient,QString("sender@example.com")); QCOMPARE(plan.body,c.withoutAttachmentMessage);
        m.attachments={{"a.pdf","application/pdf","pdf"}};
        selection=selectAttachments(m,c);
        plan=planReply(m,selection,1,c);
        QVERIFY(plan.recipient.isEmpty()); QCOMPARE(plan.skipReason,QString("有附件回复未启用"));
        c.withAttachmentReply=true; c.withAttachmentMessage="已收取";
        plan=planReply(m,selection,1,c); QCOMPARE(plan.recipient,QString("sender@example.com"));
        m.attachments.push_back({"b.exe","application/octet-stream","exe"});
        c.strict=true; selection=selectAttachments(m,c);
        plan=planReply(m,selection,0,c);
        QVERIFY(plan.recipient.isEmpty()); QCOMPARE(plan.skipReason,QString("拒收回复未启用"));
        c.reply=true; c.rejection="拒收：{reason}";
        plan=planReply(m,selection,0,c);
        QCOMPARE(plan.recipient,QString("sender@example.com")); QVERIFY(plan.body.startsWith("拒收："));
        m.headers["auto-submitted"]="auto-replied";
        plan=planReply(m,selection,0,c);
        QVERIFY(plan.recipient.isEmpty()); QVERIFY(plan.skipReason.contains("自动邮件"));
    }
    void replyRulesAreExclusiveAndRequireSavedFiles() {
        Config c; c.email="me@qq.com";
        c.withAttachmentReply=true; c.withoutAttachmentReply=true; c.reply=true;
        c.withAttachmentMessage="成功 {attachment_count} 个\n谢谢";
        c.withoutAttachmentMessage="请补发附件"; c.rejection="拒收：{reason}";
        Message m; m.headers["from"]="sender@example.com";
        auto s=selectAttachments(m,c);
        QCOMPARE(planReply(m,s,0,c).body,c.withoutAttachmentMessage);
        m.attachments={{"a.pdf","application/pdf","pdf"}};
        s=selectAttachments(m,c);
        QVERIFY(planReply(m,s,0,c).recipient.isEmpty()); // No acknowledgement before successful storage.
        QCOMPARE(planReply(m,s,1,c).body,QString("成功 1 个\n谢谢"));
        m.attachments.push_back({"bad.exe","application/octet-stream","exe"});
        s=selectAttachments(m,c); // Matching mode saves one of the two attachments.
        QCOMPARE(planReply(m,s,1,c).body,QString("成功 1 个\n谢谢"));
        c.strict=true; s=selectAttachments(m,c);
        QCOMPARE(planReply(m,s,0,c).body,"拒收："+s.reason);
        c.reply=false;
        QVERIFY(planReply(m,s,0,c).recipient.isEmpty()); // Rejection never falls back to success/no-attachment text.
        c.strict=false; c.withAttachmentReply=false;
        QVERIFY(planReply(m,selectAttachments(m,c),1,c).recipient.isEmpty());
    }
    void replyRulesRetainLoopProtectionAndInlineSemantics() {
        Config c; c.email="me@example.com"; c.withAttachmentReply=true; c.withoutAttachmentReply=true;
        Message m; m.headers["from"]="me@example.com";
        QVERIFY(planReply(m,selectAttachments(m,c),0,c).recipient.isEmpty());
        m.headers["from"]="sender@example.com"; m.headers["auto-submitted"]="auto-replied";
        QVERIFY(planReply(m,selectAttachments(m,c),0,c).recipient.isEmpty());
        m.attachments={{"a.pdf","application/pdf","pdf"}};
        QVERIFY(planReply(m,selectAttachments(m,c),1,c).recipient.isEmpty());
        QByteArray raw="From: sender@example.com\r\nContent-Type: image/png\r\nContent-Disposition: inline; filename=a.png\r\n\r\npng";
        auto excluded=parseMessage(raw,false);
        QCOMPARE(planReply(excluded,selectAttachments(excluded,c),0,c).body,c.withoutAttachmentMessage);
        auto included=parseMessage(raw,true);
        QCOMPARE(planReply(included,selectAttachments(included,c),1,c).body,c.withAttachmentMessage);
    }
    void replySettingsRoundTripAndOldConfigDefaults() {
        QTemporaryDir temp; QString path=temp.path()+"/config.json";
        QFile legacy(path); QVERIFY(legacy.open(QIODevice::WriteOnly)); legacy.write("{\"reply\":true,\"rejection\":\"legacy\"}"); legacy.close();
        auto old=Config::load(path); QVERIFY(!old.withAttachmentReply); QVERIFY(!old.withoutAttachmentReply);
        QVERIFY(old.reply); QCOMPARE(old.rejection,QString("legacy"));
        Config c; c.email="me@qq.com"; c.secret="test-only"; c.outputDir=temp.path();
        c.withAttachmentReply=true; c.withoutAttachmentReply=true;
        c.withAttachmentMessage="附件已收取\n谢谢"; c.withoutAttachmentMessage="未发现附件\n请补发";
        c.save(path); auto loaded=Config::load(path);
        QVERIFY(loaded.withAttachmentReply); QVERIFY(loaded.withoutAttachmentReply);
        QCOMPARE(loaded.withAttachmentMessage,c.withAttachmentMessage);
        QCOMPARE(loaded.withoutAttachmentMessage,c.withoutAttachmentMessage);
        c.withAttachmentMessage="  "; QVERIFY(!c.validate().isEmpty());
        c.withAttachmentReply=false; c.withoutAttachmentMessage="\n"; QVERIFY(!c.validate().isEmpty());
        c.withoutAttachmentReply=false; QVERIFY(c.validate().isEmpty());
    }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "CoreTest.moc"
