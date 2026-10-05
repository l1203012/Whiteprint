#pragma once
#include "bridge/Bridge.h"
#include "bridge/ToolSchema.h"

#include <QJsonObject>
#include <QList>
#include <functional>
#include <optional>
#include <vector>

namespace wp {

/// Validated, normalised tool arguments.
struct ToolArguments {
    QJsonObject values;

    /// Throws ArgumentError(`required`) when absent.
    QString string(const QString &key) const;
    std::optional<QString> optionalString(const QString &key) const;
    int integer(const QString &key) const;
    std::optional<int> optionalInt(const QString &key) const;

    /// A folder relative to the notes folder, with surrounding slashes trimmed. Nullopt when
    /// absent or empty; absolute paths and `..` are refused.
    std::optional<QString> folder(const QString &key) const;

    QList<StudyPoint> points(const QString &key) const;
    QList<Flashcard> cards(const QString &key) const;
    QStringList strings(const QString &key) const;
    StudyPlan plan(const QString &key) const;
};

/// One MCP tool: what Claude sees in `tools/list`, and how its arguments become a
/// `BridgeRequest`. Descriptions are sent on every turn, so they stay terse.
/// Published through `MCPToolCatalog`.
struct MCPTool {
    enum class Effect { readOnly, additive, destructive };

    QString name;
    QString description;
    std::vector<ToolSchema::Property> arguments;
    Effect effect = Effect::readOnly;
    std::function<BridgeRequest(const ToolArguments &)> request;

    AgentTool agentTool() const;

    /// Validates raw `arguments` and maps them to the request for the app.
    /// Throws ArgumentError.
    BridgeRequest makeRequest(const QJsonObject &arguments) const;

    /// Every tool, in `tools/list` order.
    static const QList<MCPTool> &all();
    static const MCPTool *named(const QString &name);
};

} // namespace wp
