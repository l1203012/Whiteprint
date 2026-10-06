#pragma once
// Port of ToolStatus.swift: the friendly progress line shown while an agent runs a tool,
// shared by ClaudeCodeRunner and GrokRunner.
#include "study/Study.h"

#include <QJsonObject>
#include <functional>
#include <optional>

namespace wp {

/// Looks up an import by id for progress messages.
using ImportInfo = std::function<std::optional<StudyImport>(const QString &id)>;

namespace ToolStatus {

inline const QString mcpPrefix = QStringLiteral("mcp__whiteprint__");

/// E.g. "Reading chunk 3 of 12 · Lecture3.pptx". `tool` may carry Claude Code's `mcp__whiteprint__`
/// prefix; `importInfo` names imports (may be empty).
QString text(const QString &tool, const QJsonObject &input, const ImportInfo &importInfo);

} // namespace ToolStatus
} // namespace wp
