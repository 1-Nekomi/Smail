#pragma once
#include "smail/Config.h"

namespace smail {
// User-scoped Windows DPAPI. Unsupported platforms never fall back to plaintext.
class CredentialStore {
public:
    explicit CredentialStore(QString directory);
    static bool supported();
    void save(const Config &config) const;
    QString load(const Config &config) const;
    void remove(const Config &config) const;
private:
    QString directory;
    QByteArray scope(const Config &config) const;
    QString path(const Config &config) const;
};
}
