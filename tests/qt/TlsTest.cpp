#include "smail/TlsConnection.h"
#include <QtTest>
#include <QTcpServer>
#include <QSslConfiguration>
#include <QSslKey>
#include <QElapsedTimer>
#include <QTimer>

using namespace smail;
class DelayedServer : public QTcpServer {
public:
    int delay = 700;
    bool handshake = true;
protected:
    void incomingConnection(qintptr descriptor) override {
        auto *socket = new QSslSocket(this);
        socket->setSocketDescriptor(descriptor);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone); // Test server does not require client certificates.
        QFile cert(QStringLiteral(FIXTURE_DIR "/localhost-cert.pem"));
        QFile key(QStringLiteral(FIXTURE_DIR "/localhost-key.pem"));
        if (!cert.open(QIODevice::ReadOnly) || !key.open(QIODevice::ReadOnly)) qFatal("Missing localhost TLS fixture");
        socket->setLocalCertificate(QSslCertificate(cert.readAll()));
        socket->setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
        if (handshake) QTimer::singleShot(delay, socket, &QSslSocket::startServerEncryption);
    }
};
class TlsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        QVERIFY(QSslSocket::setActiveBackend("schannel")); // Match the deployed Windows package.
#endif
    }
    void delayedHandshakeSucceeds() {
        DelayedServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QSslSocket socket;
        QFile cert(QStringLiteral(FIXTURE_DIR "/localhost-cert.pem")); QVERIFY(cert.open(QIODevice::ReadOnly));
        auto config = socket.sslConfiguration(); config.addCaCertificate(QSslCertificate(cert.readAll()));
        socket.setSslConfiguration(config);
        QElapsedTimer elapsed; elapsed.start();
        try { establishTls(socket,"localhost",server.serverPort(),{},5000); }
        catch (const std::exception &e) { QFAIL(e.what()); }
        QVERIFY(socket.isEncrypted()); QVERIFY(elapsed.elapsed() >= 600);
    }
    void deadlineIsNotCancellationInterval() {
        DelayedServer server; server.handshake=false; QVERIFY(server.listen(QHostAddress::LocalHost));
        QSslSocket socket; QElapsedTimer elapsed; elapsed.start();
        QString error;
        try { establishTls(socket,"127.0.0.1",server.serverPort(),{},1000); }
        catch (const std::exception &e) { error=QString::fromUtf8(e.what()); }
        QVERIFY(error.contains("总时限 1000 ms"));
        QVERIFY(elapsed.elapsed() >= 900); QVERIFY(elapsed.elapsed() < 4000);
        QVERIFY(error.contains("阶段=TLS 握手")); QVERIFY(error.contains("后端="));
        QCOMPARE(socket.state(),QAbstractSocket::UnconnectedState);
    }
    void cancellationRemainsResponsive() {
        DelayedServer server; server.handshake=false; QVERIFY(server.listen(QHostAddress::LocalHost));
        QSslSocket socket; QElapsedTimer elapsed; elapsed.start(); QString error;
        try { establishTls(socket,"127.0.0.1",server.serverPort(),[&] { return elapsed.elapsed() >= 150; }); }
        catch (const std::exception &e) { error=QString::fromUtf8(e.what()); }
        QCOMPARE(error,QString("收取已停止")); QVERIFY(elapsed.elapsed() < 1500);
        QCOMPARE(socket.state(),QAbstractSocket::UnconnectedState);
    }
    void untrustedCertificateIsRejected() {
        DelayedServer server; server.delay=0; QVERIFY(server.listen(QHostAddress::LocalHost));
        QSslSocket socket; QString error;
        try { establishTls(socket,"localhost",server.serverPort(),{},5000); }
        catch (const std::exception &e) { error=QString::fromUtf8(e.what()); }
        QVERIFY2(error.contains("证书校验失败"),qPrintable(error)); QVERIFY(!socket.isEncrypted());
    }
};
QTEST_GUILESS_MAIN(TlsTest)
#include "TlsTest.moc"
