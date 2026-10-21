// Explicit diagnostic only: connects TLS but never authenticates or reads mail.
#include "smail/TlsConnection.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    auto args=app.arguments(); QTextStream output(stdout);
    if (args.size() < 3) { output << "Usage: smail_tls_probe host port [fixed|legacy] [backend]\n"; return 2; }
    if (args.size() > 4 && !QSslSocket::setActiveBackend(args[4])) { output << "Unavailable TLS backend\n"; return 2; }
    QString host=args[1]; quint16 port=args[2].toUShort();
    QSslSocket socket; QElapsedTimer elapsed; elapsed.start();
    try {
        if (args.value(3)=="legacy") {
            socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
            socket.connectToHostEncrypted(host,port);
            while (!socket.isEncrypted()) {
                if (socket.waitForEncrypted(250)) break;
                if (socket.state()==QAbstractSocket::UnconnectedState || elapsed.elapsed()>30000)
                    throw std::runtime_error(socket.errorString().toStdString());
            }
        } else smail::establishTls(socket,host,port);
        output << "OK host=" << host << " port=" << port << " elapsed_ms=" << elapsed.elapsed()
               << " backend=" << QSslSocket::activeBackend() << Qt::endl;
        return 0;
    } catch (const std::exception &e) {
        output << "FAIL elapsed_ms=" << elapsed.elapsed() << " " << e.what() << Qt::endl; return 1;
    }
}
