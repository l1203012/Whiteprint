#pragma once
// Port of KeychainItem.swift: one generic secret in Windows Credential Manager
// (CredReadW / CredWriteW / CredDeleteW; stored per user, never in Whiteprint's settings).
#include <QString>
#include <exception>
#include <optional>

namespace wp {

struct KeychainError {
    /// The Win32 error code.
    unsigned long status = 0;
    QString description() const;
};

/// The credential's target name is `<service>/<account>`; the secret is stored as UTF-8 (limit 2560 bytes).
struct KeychainItem {
    QString service;
    QString account;

    /// The stored secret, or nullopt when there is none.
    std::optional<QString> read() const;

    /// Stores `value`, replacing what was there. Throws KeychainError.
    void save(const QString &value) const;

    /// Removes the secret; a missing one is not an error. Throws KeychainError.
    void remove() const;

    QString targetName() const { return service + QLatin1Char('/') + account; }
};

} // namespace wp
