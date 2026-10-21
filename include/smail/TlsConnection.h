#pragma once
#include <QSslSocket>
#include <functional>

namespace smail {
// Runs a local event loop in the caller's worker thread. The timeout applies to
// the entire DNS/TCP/TLS operation, not individual cancellation checks.
void establishTls(QSslSocket &socket, const QString &host, quint16 port,
                  const std::function<bool()> &cancelled = {}, int timeoutMs = 30000);
}
