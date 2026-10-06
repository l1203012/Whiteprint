#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <cmath>
#include <optional>

namespace wp {

/// Thrown by the `fromJson` functions when JSON doesn't match the expected shape
/// (the equivalent of Swift's `DecodingError`).
struct DecodingError {
    QString message;
};

namespace json {

inline QString requiredString(const QJsonObject &o, const QString &key)
{
    const QJsonValue v = o.value(key);
    if (!v.isString())
        throw DecodingError{QStringLiteral("missing or invalid string '%1'").arg(key)};
    return v.toString();
}

/// Missing or null gives nullopt; any other non-string value is an error.
inline std::optional<QString> optionalString(const QJsonObject &o, const QString &key)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
        return std::nullopt;
    if (!v.isString())
        throw DecodingError{QStringLiteral("invalid string '%1'").arg(key)};
    return v.toString();
}

inline int intValue(const QJsonValue &v, const QString &key)
{
    if (!v.isDouble() || v.toDouble() != std::floor(v.toDouble()))
        throw DecodingError{QStringLiteral("invalid integer '%1'").arg(key)};
    return v.toInt();
}

inline std::optional<int> optionalInt(const QJsonObject &o, const QString &key)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
        return std::nullopt;
    return intValue(v, key);
}

inline QJsonArray requiredArray(const QJsonObject &o, const QString &key)
{
    const QJsonValue v = o.value(key);
    if (!v.isArray())
        throw DecodingError{QStringLiteral("missing or invalid array '%1'").arg(key)};
    return v.toArray();
}

/// Missing or null gives an empty array; any other non-array value is an error.
inline QJsonArray optionalArray(const QJsonObject &o, const QString &key)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
        return {};
    if (!v.isArray())
        throw DecodingError{QStringLiteral("invalid array '%1'").arg(key)};
    return v.toArray();
}

inline QJsonObject objectValue(const QJsonValue &v, const QString &what)
{
    if (!v.isObject())
        throw DecodingError{QStringLiteral("expected an object for '%1'").arg(what)};
    return v.toObject();
}

} // namespace json
} // namespace wp
