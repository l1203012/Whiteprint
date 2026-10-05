#pragma once
#include <QList>
#include <QString>
#include <optional>

namespace wp {

/// A range of UTF-16 code units (Foundation's `NSRange`).
struct TextRange {
    int location = 0;
    int length = 0;

    constexpr TextRange() = default;
    constexpr TextRange(int location, int length) : location(location), length(length) {}
    constexpr int end() const { return location + length; }
    constexpr bool contains(int index) const { return index >= location && index < end(); }
    constexpr bool operator==(const TextRange &) const = default;
};

/// An open fenced code block: the fence character and how long its run was.
struct MarkdownFenceState {
    char16_t marker = 0;
    int length = 0;

    MarkdownFenceState() = default;
    MarkdownFenceState(char16_t marker, int length) : marker(marker), length(length) {}

    /// A compact string form, stored as a text property between restyles.
    QString encoded() const;
    static std::optional<MarkdownFenceState> fromEncoded(const QString &encoded);

    bool operator==(const MarkdownFenceState &) const = default;
};

enum class MarkdownLineKind {
    blank,
    paragraph,
    heading, // `MarkdownLine::level`
    bullet,
    ordered,
    task, // `MarkdownLine::checked`
    quote,
    divider,
    /// The opening or closing line of a fenced code block.
    fence,
    /// A line inside a fenced code block.
    code
};

/// How one line of Markdown is styled as a block.
struct MarkdownLine {
    MarkdownLineKind kind = MarkdownLineKind::blank;
    /// Heading level 1...6 (kind == heading).
    int level = 0;
    /// Ticked box (kind == task).
    bool checked = false;
    /// UTF-16 length of the leading markup (indent, marker and the space after
    /// it), e.g. `## `, `  - [ ] `, `> `. Inline parsing starts after it.
    int markerLength = 0;

    static MarkdownLine of(MarkdownLineKind kind, int markerLength = 0)
    {
        MarkdownLine l;
        l.kind = kind;
        l.markerLength = markerLength;
        return l;
    }
    static MarkdownLine heading(int level, int markerLength)
    {
        MarkdownLine l = of(MarkdownLineKind::heading, markerLength);
        l.level = level;
        return l;
    }
    static MarkdownLine task(bool checked, int markerLength)
    {
        MarkdownLine l = of(MarkdownLineKind::task, markerLength);
        l.checked = checked;
        return l;
    }
    bool isHeading() const { return kind == MarkdownLineKind::heading; }
    bool operator==(const MarkdownLine &) const = default;
};

/// An inline element. `range` covers the markup, `contentRange` the text
/// between it; both are UTF-16 ranges in the line.
struct MarkdownSpan {
    enum class Kind { bold, italic, code, link };

    Kind kind = Kind::bold;
    TextRange range;
    TextRange contentRange;
    /// The destination of a link.
    std::optional<QString> url;

    /// The parts of `range` outside `contentRange`.
    QList<TextRange> markupRanges() const;
    bool operator==(const MarkdownSpan &) const = default;
};

/// Line and inline Markdown recognition for the styler. Works on UTF-16
/// offsets, which are also `QTextDocument` positions.
namespace MarkdownSyntax {

struct Classified {
    MarkdownLine line;
    std::optional<MarkdownFenceState> openFence;
};

/// Classifies `line` (without its line break). `openFence` is the fenced
/// code block the line starts in; the result says which one it leaves open.
Classified classify(const QString &line, const std::optional<MarkdownFenceState> &openFence);

/// Inline code, links, bold and italic in `text`, with ranges offset by `offset`.
QList<MarkdownSpan> spans(const QString &text, int offset = 0);

constexpr char16_t space = 0x20, tab = 0x09;
constexpr char16_t backtick = 0x60, tilde = 0x7E, hash = 0x23;
constexpr char16_t dash = 0x2D, star = 0x2A, plus = 0x2B, underscore = 0x5F;
constexpr char16_t openBracket = 0x5B, closeBracket = 0x5D;
constexpr char16_t openParen = 0x28, closeParen = 0x29;
constexpr char16_t lowerX = 0x78, upperX = 0x58;
constexpr char16_t dot = 0x2E, greater = 0x3E, backslash = 0x5C;

constexpr bool isWhitespace(char16_t c)
{
    return c == space || c == tab;
}
constexpr bool isDigit(char16_t c)
{
    return c >= 0x30 && c <= 0x39;
}
/// Letters, digits and anything non-ASCII, for the intraword `_` rule.
constexpr bool isWordCharacter(char16_t c)
{
    return isDigit(c) || (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A) || c > 0x7F;
}
constexpr bool isASCIIPunctuation(char16_t c)
{
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) || (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

} // namespace MarkdownSyntax

} // namespace wp
