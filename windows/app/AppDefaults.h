#pragma once
// Port of AppDefaults.swift: the user defaults Whiteprint's own preferences live in (QSettings; the
// registry under HKCU\Software\Whiteprint on Windows).
#include <QSettings>
#include <QString>
#include <optional>

namespace wp {

class AppDefaults {
public:
    /// Names a separate defaults suite, so a trial run doesn't touch real preferences.
    static QString suiteEnvironmentKey() { return QStringLiteral("WHITEPRINT_DEFAULTS_SUITE"); }

    /// The shared store: `WHITEPRINT_DEFAULTS_SUITE` selects another application name, else the real one.
    static QSettings &store();

    /// A string value, nullopt when the key is missing.
    static std::optional<QString> string(const QSettings &defaults, const QString &key);
};

} // namespace wp
