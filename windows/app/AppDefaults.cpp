#include "app/AppDefaults.h"

namespace wp {

QSettings &AppDefaults::store()
{
    static QSettings *settings = [] {
        const QString suite = qEnvironmentVariable(suiteEnvironmentKey().toLatin1().constData());
        const QString application = suite.isEmpty() ? QStringLiteral("Whiteprint") : QStringLiteral("Whiteprint-") + suite;
        return new QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("Whiteprint"), application);
    }();
    return *settings;
}

std::optional<QString> AppDefaults::string(const QSettings &defaults, const QString &key)
{
    if (!defaults.contains(key))
        return std::nullopt;
    return defaults.value(key).toString();
}

} // namespace wp
