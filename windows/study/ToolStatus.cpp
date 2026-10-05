#include "study/ToolStatus.h"

#include <cmath>
#include <initializer_list>

namespace wp::ToolStatus {

namespace {

std::optional<QString> stringField(const QJsonObject &o, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isUndefined() && !v.isNull())
            return v.isString() ? std::optional<QString>(v.toString()) : std::nullopt;
    }
    return std::nullopt;
}

std::optional<int> intField(const QJsonObject &o, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isUndefined() && !v.isNull()) {
            if (v.isDouble() && v.toDouble() == std::floor(v.toDouble()))
                return int(v.toDouble());
            return std::nullopt;
        }
    }
    return std::nullopt;
}

QString reading(const QJsonObject &input, const ImportInfo &importInfo)
{
    const auto id = stringField(input, {"import", "importID", "id"});
    const auto n = intField(input, {"n", "chunk"});
    if (!id || !n)
        return QStringLiteral("Reading…");
    const auto info = importInfo ? importInfo(*id) : std::nullopt;
    if (!info)
        return QStringLiteral("Reading chunk %1 · %2").arg(*n).arg(*id);
    return QStringLiteral("Reading chunk %1 of %2 · %3").arg(*n).arg(info->chunkCount).arg(info->name);
}

} // namespace

QString text(const QString &tool, const QJsonObject &input, const ImportInfo &importInfo)
{
    const QString name = tool.startsWith(mcpPrefix) ? tool.mid(mcpPrefix.size()) : tool;
    if (name == "list_imports") return QStringLiteral("Looking at your imports…");
    if (name == "read_chunk") return reading(input, importInfo);
    if (name == "save_points") return QStringLiteral("Saving points…");
    if (name == "get_points") return QStringLiteral("Merging the points…");
    if (name == "build_study_plan") return QStringLiteral("Building the study plan…");
    if (name == "create_flashcards") return QStringLiteral("Making flashcards…");
    if (name == "ReadMcpResourceTool" || name == "ListMcpResourcesTool") return QStringLiteral("Reading the drawing reference…");
    return QStringLiteral("Working…");
}

} // namespace wp::ToolStatus
