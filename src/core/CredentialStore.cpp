#include "smail/CredentialStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#endif

namespace smail {
CredentialStore::CredentialStore(QString dir) : directory(std::move(dir)) {}
bool CredentialStore::supported() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
QByteArray CredentialStore::scope(const Config &c) const {
    return ("Smail/credential/v1\n" + c.email.trimmed().toLower() + "\n" +
        c.imapHost.toLower() + ":" + QString::number(c.imapPort) + "\n" +
        c.smtpHost.toLower() + ":" + QString::number(c.smtpPort)).toUtf8();
}
QString CredentialStore::path(const Config &c) const {
    return QDir(directory).filePath(QString::fromLatin1(
        QCryptographicHash::hash(scope(c), QCryptographicHash::Sha256).toHex()) + ".dpapi");
}
void CredentialStore::save(const Config &c) const {
    if (c.secret.isEmpty() || c.secret.size() > 8192) fail("授权码为空或长度超限");
#ifdef Q_OS_WIN
    QByteArray plain = c.secret.toUtf8(), entropy = scope(c);
    DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE *>(plain.data())};
    DATA_BLOB extra{DWORD(entropy.size()), reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB out{};
    BOOL ok = CryptProtectData(&in, L"Smail mailbox credential", &extra, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &out);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    SecureZeroMemory(plain.data(), SIZE_T(plain.size()));
    if (!ok) fail(QString("Windows 加密授权码失败（系统错误 %1），未保存明文").arg(error));
    QByteArray encrypted(reinterpret_cast<const char *>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    if (!QDir().mkpath(directory)) fail("无法创建加密凭据目录");
    if (QFileInfo(path(c)).isSymLink()) fail("凭据路径不可为符号链接");
    QSaveFile f(path(c));
    if (!f.open(QIODevice::WriteOnly) || f.write(encrypted) != encrypted.size() || !f.commit())
        fail("加密授权码保存失败");
#else
    Q_UNUSED(c)
    fail("当前平台尚不支持加密保存授权码");
#endif
}
QString CredentialStore::load(const Config &c) const {
    QFile f(path(c));
    if (!f.exists()) return {};
#ifdef Q_OS_WIN
    if (QFileInfo(f).isSymLink() || !f.open(QIODevice::ReadOnly) || f.size() > 65536)
        fail("无法读取加密授权码，请重新输入并保存");
    QByteArray encrypted = f.readAll(), entropy = scope(c);
    DATA_BLOB in{DWORD(encrypted.size()), reinterpret_cast<BYTE *>(encrypted.data())};
    DATA_BLOB extra{DWORD(entropy.size()), reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        fail("授权码解密失败，可能已损坏或 Windows 用户已改变；请重新输入并保存");
    QString result = QString::fromUtf8(reinterpret_cast<const char *>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
#else
    fail("当前平台无法解密 Windows 授权码");
    return {};
#endif
}
void CredentialStore::remove(const Config &c) const {
    QString file = path(c);
    if ((QFileInfo::exists(file) || QFileInfo(file).isSymLink()) && !QFile::remove(file))
        fail("删除已保存授权码失败");
}
}
