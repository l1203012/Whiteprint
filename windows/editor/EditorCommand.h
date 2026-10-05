#pragma once
#include "editor/MarkdownEditing.h"

#include <QList>
#include <QString>
#include <optional>

namespace wp {

/// A formatting or insert command, run with `NoteEditorView::perform()`
/// from menus, the command toolbar or shortcuts.
enum class EditorCommand {
    heading1,
    heading2,
    heading3,
    body,
    bold,
    italic,
    inlineCode,
    bulletList,
    numberedList,
    checklist,
    quote,
    codeBlock,
    divider,
    drawing,
    flashcards,
    newPage
};

namespace EditorCommands {

/// Every command, in declaration order.
QList<EditorCommand> all();
/// The stable identifier (`heading1`, `bold`, ...).
QString rawValue(EditorCommand command);
std::optional<EditorCommand> fromRawValue(const QString &raw);
/// Menu and button title (`Heading 1`, `Bold`, ...).
QString title(EditorCommand command);
/// A short glyph label for buttons: `H1`, `B`, `I`, ... (headings and body use text labels).
QString glyph(EditorCommand command);
/// True for the commands that rewrite the focused text block (everything but drawing, flashcards, new page).
bool isTextCommand(EditorCommand command);

} // namespace EditorCommands

/// Text commands as pure functions from text and selection to a `TextChange`.
namespace MarkdownFormatting {

/// Runs a text command. Insert commands (drawing, flashcards, new page)
/// aren't text edits and return nullopt.
std::optional<TextChange> apply(EditorCommand command, const QString &text, TextRange selection);

/// Wraps the selection in `marker`, or unwraps it when it's already
/// wrapped (markers just inside or just outside the selection). With no
/// selection, inserts a pair with the caret between them, or removes an
/// empty pair around the caret. A multi-line selection is wrapped line
/// by line.
std::optional<TextChange> toggleWrap(const QString &marker, const QString &text, TextRange selection);

/// Gives every line the selection touches the block kind `kind` (any
/// existing heading, list or quote marker is replaced). When they all
/// have it already, it's toggled off back to body text. Ordered lists
/// are numbered from 1; code lines are left alone.
std::optional<TextChange> setLineKind(const LineKind &kind, const QString &text, TextRange selection);

} // namespace MarkdownFormatting

} // namespace wp
