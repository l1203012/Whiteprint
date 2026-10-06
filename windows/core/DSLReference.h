#pragma once
#include <QString>

/// Text served to Claude. Kept short: it costs tokens every time it's read.
namespace wp::WhiteprintText {

/// The drawing language reference, served as MCP resource `whiteprint://dsl`.
/// Keep in sync with docs/DSL.md.
QString dslReference();

} // namespace wp::WhiteprintText
