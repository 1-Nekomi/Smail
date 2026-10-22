#include "smail/MailClient.h"
#include "smail/ReplyPolicy.h"
#include "smail/Collector.h"
#include "smail/Store.h"
#include <QTemporaryDir>
#include <QtTest>
#include <QSslServer>
#include <QSslKey>
#include <QSslConfiguration>
#include <QThread>
#include <future>
#include <memory>

class SmtpWorker : public QThread {
public:
    std::promise<quint16> port;
    QByteArray message, recipient;
    QList<QByteArray> messages;
    bool accepted = false, authenticated = false;
    int deliveries = 0;
protected:
    void run() override {
        QSslServer server;
        auto config = QSslConfiguration::defaultConfiguration();
        QFile cert(QStringLiteral(FIXTURE_DIR "/localhost-cert.pem"));
        QFile key(QStringLiteral(FIXTURE_DIR "/localhost-key.pem"));
        if (!cert.open(QIODevice::ReadOnly) || !key.open(QIODevice::ReadOnly)) {
            port.set_value(0);
            return;
        }
        config.setLocalCertificate(QSslCertificate(cert.readAll()));
        config.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        server.setSslConfiguration(config);
        // QSslServer emits newConnection() before the TLS handshake has been
        // added to the pending queue; pendingConnectionAvailable() fires after
        // the socket is encrypted and ready for nextPendingConnection().
        QObject::connect(&server, &QTcpServer::pendingConnectionAvailable, &server, [&] {
            auto *socket = server.nextPendingConnection();
            if (!socket) return;
            auto phase = std::make_shared<int>(0);
            socket->write("220 localhost test SMTP\r\n");
            QObject::connect(socket, &QIODevice::readyRead, socket, [this, socket, phase] {
                while (socket->canReadLine()) {
                    auto line = socket->readLine();
                    if (*phase == 4) {
                        if (line == ".\r\n") {
                            accepted = true;
                            ++deliveries;
                            messages << message;
                            *phase = 5;
                            socket->write("250 accepted\r\n");
                        } else message += line;
                    } else if (line.startsWith("EHLO ")) {
                        socket->write("250-localhost\r\n250 AUTH LOGIN\r\n");
                    } else if (line == "AUTH LOGIN\r\n") {
                        *phase = 1;
                        socket->write("334 VXNlcm5hbWU6\r\n");
                    } else if (*phase == 1) {
                        *phase = 2;
                        socket->write("334 UGFzc3dvcmQ6\r\n");
                    } else if (*phase == 2) {
                        *phase = 3;
                        authenticated = true;
                        socket->write("235 authenticated\r\n");
                    } else if (line.startsWith("MAIL FROM:")) {
                        socket->write("250 ok\r\n");
                    } else if (line.startsWith("RCPT TO:")) {
                        recipient = line.trimmed();
                        socket->write("250 ok\r\n");
                    } else if (line == "DATA\r\n") {
                        message.clear();
                        *phase = 4;
                        socket->write("354 send data\r\n");
                    } else if (line == "QUIT\r\n") {
                        socket->write("221 bye\r\n");
                        socket->disconnectFromHost();
                    } else {
                        socket->write("500 unexpected command\r\n");
                    }
                }
            });
        });
        if (!server.listen(QHostAddress::LocalHost)) {
            port.set_value(0);
            return;
        }
        port.set_value(server.serverPort());
        exec();
    }
};

class SmtpTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        QVERIFY(QSslSocket::setActiveBackend("schannel"));
#endif
        QFile cert(QStringLiteral(FIXTURE_DIR "/localhost-cert.pem"));
        QVERIFY(cert.open(QIODevice::ReadOnly));
        original = std::make_unique<QSslConfiguration>(QSslConfiguration::defaultConfiguration());
        auto trusted = *original;
        trusted.addCaCertificate(QSslCertificate(cert.readAll()));
        QSslConfiguration::setDefaultConfiguration(trusted);
    }
    void cleanupTestCase() { if (original) QSslConfiguration::setDefaultConfiguration(*original); }

    void deliverBothReplyKindsToLocalTlsServer() {
        for (bool attachment : {false, true}) {
            smail::Config c;
            c.email = "collector@example.com";
            c.secret = "dummy-local-test";
            c.smtpHost = "localhost";
            c.withAttachmentReply = true;
            c.withoutAttachmentReply = true;
            c.withAttachmentMessage = "附件收到，谢谢\n测试成功";
            c.withoutAttachmentMessage = "未发现附件，请补发\n测试成功";
            smail::Message message;
            message.headers["from"] = "\"nekomi.typedef@gmail.com\" <nekomi.typedef@gmail.com>";
            if (attachment) message.attachments = {{"a.pdf", "application/pdf", "pdf"}};
            auto selection = smail::selectAttachments(message, c);
            auto plan = smail::planReply(message, selection, attachment ? 1 : 0, c);
            QCOMPARE(plan.recipient, QString("nekomi.typedef@gmail.com"));
            SmtpWorker server;
            auto port = server.port.get_future();
            server.start();
            c.smtpPort = port.get();
            QString error;
            if (!c.smtpPort) error = "Local SMTP server failed to start";
            else try { smail::MailClient::sendReply(c, plan.recipient, plan.body); }
                 catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
            server.quit();
            server.wait();
            if (!error.isEmpty()) QFAIL(qPrintable(error));
            QVERIFY(server.accepted);
            QCOMPARE(server.recipient, QByteArray("RCPT TO:<nekomi.typedef@gmail.com>"));
            QVERIFY(server.message.contains("Auto-Submitted: auto-replied\r\n"));
            auto split = server.message.indexOf("\r\n\r\n");
            QVERIFY(split > 0);
            QCOMPARE(QString::fromUtf8(QByteArray::fromBase64(server.message.mid(split + 4))),
                     attachment ? c.withAttachmentMessage : c.withoutAttachmentMessage);
        }
    }

    void authenticationProbeDoesNotSendMail() {
        smail::Config c;
        c.email = "collector@example.com";
        c.secret = "dummy-local-test";
        c.smtpHost = "localhost";
        SmtpWorker server;
        auto port = server.port.get_future();
        server.start();
        c.smtpPort = port.get();
        QString error;
        if (!c.smtpPort) error = "Local SMTP server failed to start";
        else try { smail::MailClient::testSmtp(c); }
             catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
        server.quit();
        server.wait();
        if (!error.isEmpty()) QFAIL(qPrintable(error));
        QVERIFY(server.authenticated);
        QVERIFY(!server.accepted);
    }

    void startupUnreadCollection_data() {
        QTest::addColumn<bool>("existingCheckpoint");
        QTest::newRow("first-start") << false;
        QTest::newRow("existing-mailbox") << true;
    }
    void startupUnreadCollection() {
        QFETCH(bool, existingCheckpoint);
        QTemporaryDir temp;
        smail::Config c;
        c.email = "collector@example.com"; c.secret = "local-test";
        c.imapHost = c.smtpHost = "localhost";
        c.outputDir = temp.path() + "/files";
        c.withAttachmentReply = c.withoutAttachmentReply = true;
        c.withAttachmentMessage = "attachment-received";
        c.withoutAttachmentMessage = "no-attachment-received";
        c.withAttachmentSubject = "收取完成：中文主题";
        c.withoutAttachmentSubject = "请补发附件：中文主题";
        QFile replyFile(temp.filePath("说明图片.png"));
        QVERIFY(replyFile.open(QIODevice::WriteOnly));
        const QByteArray replyBytes = QByteArray::fromHex("89504e470d0a1a0a0000ff");
        replyFile.write(replyBytes); replyFile.close();
        c.withAttachmentFiles = c.withoutAttachmentFiles = {replyFile.fileName()};
        const QString path = temp.path() + "/history.sqlite";
        // accountKey includes the port, so populate history after the listener starts.
        QSslServer imap;
        auto tls = QSslConfiguration::defaultConfiguration();
        QFile cert(QStringLiteral(FIXTURE_DIR "/localhost-cert.pem"));
        QFile key(QStringLiteral(FIXTURE_DIR "/localhost-key.pem"));
        QVERIFY(cert.open(QIODevice::ReadOnly)); QVERIFY(key.open(QIODevice::ReadOnly));
        tls.setLocalCertificate(QSslCertificate(cert.readAll()));
        tls.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
        tls.setPeerVerifyMode(QSslSocket::VerifyNone); imap.setSslConfiguration(tls);
        QVERIFY(imap.listen(QHostAddress::LocalHost)); c.imapPort = imap.serverPort();
        SmtpWorker smtp;
        auto port = smtp.port.get_future(); smtp.start(); c.smtpPort = port.get();
        struct StopServer { SmtpWorker &s; ~StopServer() { s.quit(); s.wait(); } } stop{smtp};
        QVERIFY(c.smtpPort);
        const auto box = c.accountKey() + "-77";
        {
            smail::Store store(path);
            if (existingCheckpoint) store.checkpoint(box, "10");
            smail::Record r; r.id = box + "-4"; r.done = true; r.replyState = "sent"; store.put(r);
        }
        QList<quint64> fetched, marked;
        int unreadSearches = 0;
        QStringList unexpected;
        QObject::connect(&imap, &QTcpServer::pendingConnectionAvailable, &imap, [&] {
            auto *socket = imap.nextPendingConnection();
            socket->write("* OK local IMAP\r\n");
            QObject::connect(socket, &QIODevice::readyRead, socket, [&, socket] {
                while (socket->canReadLine()) {
                    auto line = socket->readLine().trimmed();
                    auto tag = line.left(line.indexOf(' '));
                    auto cmd = line.mid(tag.size() + 1);
                    if (cmd.startsWith("LOGIN ")) {}
                    else if (cmd == "SELECT INBOX") socket->write("* OK [UIDVALIDITY 77] stable\r\n");
                    else if (cmd == "UID SEARCH ALL") socket->write("* SEARCH 1 2 3 4 10\r\n");
                    else if (cmd == "UID SEARCH UNSEEN") {
                        ++unreadSearches;
                        socket->write("* SEARCH 2 3 4\r\n");
                    } else if (cmd.startsWith("UID SEARCH UID ")) socket->write("* SEARCH 11\r\n");
                    else if (cmd.startsWith("UID FETCH ")) {
                        const auto uid = cmd.split(' ').at(2).toULongLong();
                        if (cmd.endsWith("(RFC822.SIZE)")) socket->write("* 1 FETCH (RFC822.SIZE 512)\r\n");
                        else {
                            fetched << uid;
                            QByteArray body = "From: sender@example.com\r\nSubject: local test\r\n";
                            body += uid == 2 ? "Content-Type: application/pdf\r\nContent-Disposition: attachment; filename=test.pdf\r\n\r\npdf"
                                             : "Content-Type: text/plain\r\n\r\nhello";
                            socket->write("* 1 FETCH (BODY[] {" + QByteArray::number(body.size()) + "}\r\n" + body + ")\r\n");
                        }
                    } else if (cmd.startsWith("UID STORE ")) marked << cmd.split(' ').at(2).toULongLong();
                    else unexpected << QString::fromLatin1(cmd);
                    socket->write(tag + " OK done\r\n");
                }
            });
        });
        for (int run = 0; run < 2; ++run) {
            smail::Collector collector(c, path);
            QSignalSpy activity(&collector, &smail::Collector::activity);
            collector.start();
            bool completed = false;
            QElapsedTimer timer; timer.start();
            while (!completed && timer.elapsed() < 15000) {
                QTest::qWait(20);
                for (const auto &args : activity)
                    if (args[0].toString().startsWith("本轮检查完成")) completed = true;
            }
            collector.requestInterruption(); collector.wait();
            QVERIFY2(completed, "Collector did not finish the local unread/new-mail cycle");
        }
        smtp.quit(); smtp.wait();
        QCOMPARE(unreadSearches, 2);
        QCOMPARE(fetched, QList<quint64>({2, 3, 11}));
        QCOMPARE(marked, QList<quint64>({2, 3, 11}));
        QCOMPARE(smtp.deliveries, 3);
        for (int i = 0; i < smtp.messages.size(); ++i) {
            auto parsed = smail::parseMessage(smtp.messages[i]);
            QCOMPARE(smail::decodeHeader(parsed.headers.value("subject")), i == 0 ? c.withAttachmentSubject : c.withoutAttachmentSubject);
            QCOMPARE(parsed.attachments.size(), 1);
            QCOMPARE(parsed.attachments[0].name, QString("说明图片.png"));
            QCOMPARE(parsed.attachments[0].data, replyBytes);
        }
        QVERIFY(unexpected.isEmpty());
        smail::Store store(path);
        QCOMPARE(store.checkpoint(box), QString("11"));
        QVERIFY(store.pendingUnread(box).isEmpty());
        for (auto uid : {2, 3, 11}) {
            auto r = store.record(box + "-" + QString::number(uid));
            QVERIFY(r.done); QCOMPARE(r.replyState, QString("sent"));
        }
        QVERIFY(QFile::exists(c.outputDir + "/test.pdf"));
        QVERIFY(!store.record(box + "-1").exists); // Previously read mail stays untouched.
    }
private:
    std::unique_ptr<QSslConfiguration> original;
};
QTEST_GUILESS_MAIN(SmtpTest)
#include "SmtpTest.moc"
