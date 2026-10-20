#include "smail/Config.h"
#include "smail/Mime.h"
#include "smail/Store.h"
#include "smail/MailClient.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

using namespace smail;
class CoreTest : public QObject {
    Q_OBJECT
private slots:
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
};
QTEST_GUILESS_MAIN(CoreTest)
#include "CoreTest.moc"
