#pragma once
#include <QString>

namespace wp::WhiteprintText {

/// Instructions for building a study plan, served as MCP prompt `study_plan`
/// and passed to `claude -p` by the one-click button (followed by an
/// `Imports: i1, i2` line).
QString studyPlanPrompt();

/// Instructions for making flashcards from a note or an import, served as
/// MCP prompt `flashcards` (followed by a `Source: n3` line).
QString flashcardsPrompt();

} // namespace wp::WhiteprintText
