#pragma once
#include "editor/MarkdownLine.h"
#include "editor/SlashCommand.h"

#include <QString>
#include <optional>

namespace wp {

/// A replacement in a text block. `range` is in the text before the change,
/// `selection` in the text after it; both are UTF-16.
struct TextChange {
    TextRange range;
    QString replacement;
    TextRange selection;

    QString applied(const QString &text) const
    {
        QString result = text;
        result.replace(range.location, range.length, replacement);
        return result;
    }
    bool operator==(const TextChange &) const = default;
};

/// The Notion-style editing behaviours, as pure functions from text and
/// selection to a `TextChange`. nullopt means "let the text view do its default".
namespace MarkdownEditing {

/// List and checklist items are indented by this much per level.
inline const QString indentUnit = QStringLiteral("    ");

/// Return: continues a list or checklist, ends it on an empty item (outdenting
/// a nested one first), and closes a code fence opened on this line.
std::optional<TextChange> newline(const QString &text, TextRange selection);

/// Tab / Shift+Tab on list items: indents or outdents every list line the
/// selection touches. nullopt if none of them is a list item.
std::optional<TextChange> indent(const QString &text, TextRange selection, bool outdent);

/// Typing a space after `[]`, `[ ]` or `[x]` at the start of a line turns
/// it into a checklist item.
std::optional<TextChange> shortcut(const QString &inserting, TextRange range, const QString &text);

/// The `[ ]` / `[x]` of the checklist line containing `location`.
std::optional<TextRange> checkboxRange(int location, const QString &text);

/// Ticks or unticks the checklist item on the line containing `location`.
/// The selection is left where it was.
std::optional<TextChange> toggleCheckbox(int location, const QString &text, TextRange selection);

struct SlashEffect {
    enum class Kind {
        /// Rewrite the text.
        text,
        /// Remove the typed command, then insert a drawing at the caret.
        insertDrawing,
        /// Remove the typed command, then insert a flashcard deck at the caret.
        insertDeck,
        /// Remove the typed command, then add a page after this one.
        newPage
    };
    Kind kind = Kind::text;
    TextChange change;
    bool operator==(const SlashEffect &) const = default;
};

/// Applies `command` to the line where `/query` was typed. `slash` is the
/// typed command including the slash.
SlashEffect apply(SlashCommand command, const QString &text, TextRange slash);

/// Whether a `/` typed at `location` should open the slash menu: at the
/// start of a line or after whitespace, outside code.
bool opensSlashMenu(int location, const QString &text);

/// The number an ordered item at `indent` continues with, from the
/// nearest earlier item of the same list at that indent.
std::optional<int> orderedNumber(const TextLine &before, const QString &text, const QString &indent);

} // namespace MarkdownEditing

/// Keeps a selection pointing at the same text when a block's content is
/// replaced from outside (Claude editing the note over MCP).
namespace CaretTransform {

TextRange transform(TextRange selection, const QString &from, const QString &to);

} // namespace CaretTransform

} // namespace wp
