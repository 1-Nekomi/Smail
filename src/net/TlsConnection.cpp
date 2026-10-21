#include "smail/TlsConnection.h"
#include "smail/Config.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSslError>
#include <QTimer>

namespace smail {
void establishTls(QSslSocket &socket, const QString &host, quint16 port,
                  const std::function<bool()> &cancelled, int timeoutMs) {
    if (cancelled && cancelled()) fail("收取已停止");
    if (!QSslSocket::supportsSsl()) fail("TLS 后端不可用，请检查 Qt TLS 插件");
    if (timeoutMs <= 0) fail("TLS 超时参数无效");
    QEventLoop loop;
    QTimer deadline, cancellation;
    deadline.setSingleShot(true);
    cancellation.setInterval(100);
    QElapsedTimer elapsed;
    QString stage = "DNS/TCP 建连";
    QStringList certificateErrors;
    QString transportError;
    int errorCode = -1;
    bool timedOut = false, stopped = false, failed = false;
    QObject::connect(&socket, &QSslSocket::connected, &loop, [&] { stage = "TLS 握手"; });
    QObject::connect(&socket, &QSslSocket::encrypted, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QSslSocket::errorOccurred, &loop, [&](QAbstractSocket::SocketError error) {
        failed = true; errorCode = int(error); transportError = socket.errorString(); loop.quit();
    });
    QObject::connect(&socket, &QSslSocket::sslErrors, &loop, [&](const QList<QSslError> &errors) {
        for (const auto &error : errors) certificateErrors << error.errorString();
        // Never ignore certificate errors or downgrade to plaintext.
        failed = true; loop.quit();
    });
    QObject::connect(&socket, &QSslSocket::disconnected, &loop, [&] {
        failed = true;
        if (transportError.isEmpty()) transportError = "远端在连接建立前断开";
        loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { timedOut = true; loop.quit(); });
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
        if (cancelled && cancelled()) { stopped = true; loop.quit(); }
    });
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    elapsed.start(); deadline.start(timeoutMs); cancellation.start();
    socket.connectToHostEncrypted(host, port);
    if (!socket.isEncrypted() && !failed) loop.exec();
    if (!stopped && !timedOut && !failed && socket.isEncrypted()) return;
    QString reason;
    if (stopped) reason = "收取已停止";
    else if (!certificateErrors.isEmpty()) reason = "证书校验失败：" + certificateErrors.join("；");
    else if (timedOut) reason = QString("连接超时（总时限 %1 ms）").arg(timeoutMs);
    else reason = transportError.isEmpty() ? socket.errorString() : transportError;
    QString diagnostic = QString("TLS 连接失败 [%1:%2；阶段=%3；耗时=%4 ms；后端=%5；错误码=%6]：%7")
        .arg(host).arg(port).arg(stage).arg(elapsed.elapsed()).arg(QSslSocket::activeBackend()).arg(errorCode).arg(reason);
    socket.abort();
    fail(stopped ? QStringLiteral("收取已停止") : diagnostic);
}
}
