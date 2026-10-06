#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace wp {

/// A bad tool argument, reported to Claude as `path: problem`.
struct ArgumentError {
    QString path;
    QString problem;

    ArgumentError(QString path, QString problem) : path(std::move(path)), problem(std::move(problem)) {}
    QString description() const { return path.isEmpty() ? problem : path + QStringLiteral(": ") + problem; }
};

/// The small subset of JSON Schema the tools use. One description produces both the
/// `inputSchema` sent to Claude and the validation of incoming arguments, so the two can't drift.
class ToolSchema
{
public:
    enum class Type { string, integer, oneOf, array, object };
    struct Property;

    static ToolSchema string();
    static ToolSchema integer();
    static ToolSchema oneOf(QStringList values);
    static ToolSchema array(ToolSchema items);
    static ToolSchema object(std::vector<Property> properties);

    Type type() const { return type_; }

    /// The JSON Schema, as sent in `tools/list`.
    QJsonObject json() const;

    /// Checks `value` and returns it normalised: integral numbers and numeric strings become
    /// integers. Throws ArgumentError naming the offending path, e.g. `points[2].importance`.
    QJsonValue validate(const QJsonValue &value, const QString &path = {}) const;

private:
    Type type_ = Type::string;
    QStringList values_;
    std::shared_ptr<const ToolSchema> items_;
    std::vector<Property> properties_;
};

struct ToolSchema::Property {
    QString name;
    ToolSchema schema;
    bool isRequired = false;

    static Property required(QString name, ToolSchema schema) { return {std::move(name), std::move(schema), true}; }
    static Property optional(QString name, ToolSchema schema) { return {std::move(name), std::move(schema), false}; }
};

} // namespace wp
