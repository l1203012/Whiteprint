#include "bridge/ToolSchema.h"

#include <QJsonArray>
#include <cmath>

namespace wp {

ToolSchema ToolSchema::string()
{
    ToolSchema s;
    s.type_ = Type::string;
    return s;
}

ToolSchema ToolSchema::integer()
{
    ToolSchema s;
    s.type_ = Type::integer;
    return s;
}

ToolSchema ToolSchema::oneOf(QStringList values)
{
    ToolSchema s;
    s.type_ = Type::oneOf;
    s.values_ = std::move(values);
    return s;
}

ToolSchema ToolSchema::array(ToolSchema items)
{
    ToolSchema s;
    s.type_ = Type::array;
    s.items_ = std::make_shared<const ToolSchema>(std::move(items));
    return s;
}

ToolSchema ToolSchema::object(std::vector<Property> properties)
{
    ToolSchema s;
    s.type_ = Type::object;
    s.properties_ = std::move(properties);
    return s;
}

QJsonObject ToolSchema::json() const
{
    switch (type_) {
    case Type::string:
        return {{"type", "string"}};
    case Type::integer:
        return {{"type", "integer"}};
    case Type::oneOf:
        return {{"type", "string"}, {"enum", QJsonArray::fromStringList(values_)}};
    case Type::array:
        return {{"type", "array"}, {"items", items_->json()}};
    case Type::object: {
        QJsonObject json{{"type", "object"}};
        QJsonObject props;
        QJsonArray required;
        for (const auto &p : properties_) {
            props.insert(p.name, p.schema.json());
            if (p.isRequired)
                required.append(p.name);
        }
        json.insert("properties", props);
        if (!required.isEmpty())
            json.insert("required", required);
        return json;
    }
    }
    return {};
}

QJsonValue ToolSchema::validate(const QJsonValue &value, const QString &path) const
{
    switch (type_) {
    case Type::string:
        if (!value.isString())
            throw ArgumentError(path, QStringLiteral("expected a string"));
        return value;
    case Type::integer: {
        if (value.isDouble()) {
            const double d = value.toDouble();
            if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e15)
                return QJsonValue(static_cast<qint64>(d));
        } else if (value.isString()) {
            bool ok = false;
            const qint64 n = value.toString().trimmed().toLongLong(&ok);
            if (ok)
                return QJsonValue(n);
        }
        throw ArgumentError(path, QStringLiteral("expected an integer"));
    }
    case Type::oneOf:
        if (!value.isString() || !values_.contains(value.toString()))
            throw ArgumentError(path, QStringLiteral("expected one of %1").arg(values_.join(QStringLiteral(", "))));
        return value;
    case Type::array: {
        if (!value.isArray())
            throw ArgumentError(path, QStringLiteral("expected an array"));
        QJsonArray result;
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i)
            result.append(items_->validate(array.at(i), QStringLiteral("%1[%2]").arg(path).arg(i)));
        return result;
    }
    case Type::object: {
        if (!value.isObject())
            throw ArgumentError(path, QStringLiteral("expected an object"));
        const QJsonObject object = value.toObject();
        const QString prefix = path.isEmpty() ? QString() : path + QLatin1Char('.');
        QStringList names;
        for (const auto &p : properties_)
            names.append(p.name);
        QStringList keys = object.keys();
        keys.sort();
        for (const QString &key : keys) {
            if (!names.contains(key)) {
                throw ArgumentError(prefix + key,
                                    QStringLiteral("unknown argument (expected %1)").arg(names.join(QStringLiteral(", "))));
            }
        }
        QJsonObject result;
        for (const auto &p : properties_) {
            const QJsonValue field = object.value(p.name);
            if (field.isUndefined() || field.isNull()) {
                if (p.isRequired)
                    throw ArgumentError(prefix + p.name, QStringLiteral("required"));
                continue;
            }
            result.insert(p.name, p.schema.validate(field, prefix + p.name));
        }
        return result;
    }
    }
    return value;
}

} // namespace wp
