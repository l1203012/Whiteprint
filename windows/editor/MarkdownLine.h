#pragma once
#include "render/MarkdownSyntax.h" // TextRange

#include <QChar>
#include <QList>
#include <QString>
#include <optional>

namespace wp {

/// The kind of block a line of Markdown is (editor side; the styler has its own `MarkdownLine`).
struct LineKind {
    enum class Type { paragraph, heading, bullet, ordered, checklist, quote };

    Type type = Type::paragraph;
    /// Heading level 1...6.
    int level = 0;
    /// The number of an ordered item.
    int number = 0;
    /// The bullet of a bullet or checklist item, or the delimiter of an ordered one.
    QChar mark;
    bool checked = false;

    static LineKind paragraph() { return LineKind(); }
    static LineKind heading(int level)
    {
        LineKind k;
        k.type = Type::heading;
        k.level = level;
        return k;
    }
    static LineKind bullet(QChar bullet)
    {
        LineKind k;
        k.type = Type::bullet;
        k.mark = bullet;
        return k;
    }
    static LineKind ordered(int number, QChar delimiter)
    {
        LineKind k;
        k.type = Type::ordered;
        k.number = number;
        k.mark = delimiter;
        return k;
    }
    static LineKind checklist(bool checked, QChar bullet)
    {
        LineKind k;
        k.type = Type::checklist;
        k.checked = checked;
        k.mark = bullet;
        return k;
    }
    static LineKind quote()
    {
        LineKind k;
        k.type = Type::quote;
        return k;
    }
    bool operator==(const LineKind &) const = default;
};

/// The block-level prefix of one Markdown line: indentation plus a heading,
/// list, checklist or quote marker. Lengths are UTF-16 (the prefix is ASCII).
struct MarkdownLinePrefix {
    LineKind kind;
    /// Leading spaces and tabs.
    QString indent;
    /// Indent plus marker: where the line's content starts.
    int prefixLength = 0;

    explicit MarkdownLinePrefix(const QString &line);

    bool isListItem() const;
    /// The marker for the item after this one, e.g. `2. ` after `1. `.
    std::optional<QString> continuationMarker() const;
    /// The UTF-16 range of `[ ]` / `[x]` within the line, for checklist lines.
    std::optional<TextRange> checkboxRange() const;
    bool operator==(const MarkdownLinePrefix &) const = default;
};

/// One line of a text block, without its line terminator.
struct TextLine {
    TextRange range;
    QString text;

    int start() const { return range.location; }
    int end() const { return range.end(); }
    bool operator==(const TextLine &) const = default;

    /// The line containing UTF-16 offset `location`.
    static TextLine at(int location, const QString &text);
    /// Every line in `text`, including an empty last line after a final newline.
    static QList<TextLine> all(const QString &text);
};

/// True when every character is whitespace (an empty string is).
bool isAllWhitespace(const QString &text);

/// Fenced code blocks (``` or ~~~) are where Markdown shortcuts stop applying.
namespace CodeFence {

struct Opener {
    QChar marker;
    int length = 0;
    bool operator==(const Opener &) const = default;
};

std::optional<Opener> opener(const QString &line);
bool closes(const Opener &opener, const QString &line);
/// The fence open at the start of the line beginning at `lineStart`, if any.
std::optional<Opener> open(int lineStart, const QString &text);
/// Whether the line at `lineStart` is code (inside a fence, or a fence line itself).
bool isCode(int lineStart, const QString &text);
/// Whether a fence opened on the line at `lineStart` is left unclosed.
bool isUnclosed(int lineStart, const QString &text);

} // namespace CodeFence

/// The paragraph (line including its terminator) holding `range`, like `NSString.paragraphRange(for:)`.
TextRange paragraphRange(const QString &text, TextRange range);

} // namespace wp
