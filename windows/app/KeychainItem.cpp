#include "app/KeychainItem.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace wp {

#ifdef Q_OS_WIN

QString KeychainError::description() const
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, DWORD(status), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text = length && buffer ? QString::fromWCharArray(buffer, int(length)).trimmed()
                                    : QStringLiteral("Credential Manager error %1").arg(status);
    if (buffer)
        LocalFree(buffer);
    return text;
}

std::optional<QString> KeychainItem::read() const
{
    const std::wstring target = targetName().toStdWString();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
        return std::nullopt;
    const QByteArray blob(reinterpret_cast<const char *>(credential->CredentialBlob), int(credential->CredentialBlobSize));
    CredFree(credential);
    return QString::fromUtf8(blob);
}

void KeychainItem::save(const QString &value) const
{
    const std::wstring target = targetName().toStdWString();
    const std::wstring user = account.toStdWString();
    const std::wstring comment = (QStringLiteral("Whiteprint – ") + account).toStdWString();
    QByteArray blob = value.toUtf8();
    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target.c_str());
    credential.Comment = const_cast<LPWSTR>(comment.c_str());
    credential.CredentialBlobSize = DWORD(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(user.c_str());
    if (!CredWriteW(&credential, 0))
        throw KeychainError{GetLastError()};
}

void KeychainItem::remove() const
{
    const std::wstring target = targetName().toStdWString();
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        const DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND)
            throw KeychainError{error};
    }
}

#else

QString KeychainError::description() const { return QStringLiteral("Credential storage is only available on Windows"); }
std::optional<QString> KeychainItem::read() const { return std::nullopt; }
void KeychainItem::save(const QString &) const { throw KeychainError{1}; }
void KeychainItem::remove() const {}

#endif

} // namespace wp
