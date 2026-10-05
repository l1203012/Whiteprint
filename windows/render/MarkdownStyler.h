#pragma once
#include "render/MarkdownSyntax.h"
#include "render/Palette.h"

#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextFormat>
#include <memory>
#include <optional>

class QTextDocument;

namespace wp {

/// Markdown look shared by the editor and PDF export: headings, bold/italic,
/// inline code, code blocks, lists, checklists, quotes, links. The markup
/// characters stay in the text (editing is plain Markdown) but are muted, or,
/// when concealing, marked with `markupProperty` for the editor to hide.
///
/// The styled text is a `QTextDocument`: character styles are `QTextCharFormat`s
/// (font, foreground, background, custom properties below), paragraph styles are
/// `QTextBlockFormat`s. A document position is a UTF-16 offset, like an NSRange.
/// Restyling changes formats only, never text; it is recorded as ordinary document
/// changes (`contentsChange` fires), so callers guard against re-entrance.
namespace MarkdownStyler {

inline constexpr double defaultFontSize = 15;

/// Char property (bool): the characters of a fenced code block, fence lines
/// included, e.g. so the editor can skip Markdown shortcuts there.
inline constexpr int codeBlockProperty = QTextFormat::UserProperty + 0x101;
/// Char property (QString): the destination of a `[link](url)`, on the link text.
/// Not an anchor, so text views don't restyle or auto-open it.
inline constexpr int linkProperty = QTextFormat::UserProperty + 0x102;
/// Char property (QString, a `Markup` name): markup to draw differently while
/// concealing. The characters stay in the text; only their glyphs change.
inline constexpr int markupProperty = QTextFormat::UserProperty + 0x103;
/// Block property (QString): the code fence the line starts inside of, so an
/// incremental restyle knows its context without re-reading the text above it.
inline constexpr int fenceProperty = QTextFormat::UserProperty + 0x104;
/// Char property (bool): marks a divider line in `presentation` text.
inline constexpr int ruleProperty = QTextFormat::UserProperty + 0x105;

/// How a concealed piece of markup is drawn.
enum class Markup {
    /// Not drawn and takes no space: `#`, `**`, backticks, `> `, fences, link targets.
    hidden,
    /// A list marker (`-`, `*`, `+`), drawn as a bullet.
    bullet,
    /// The `[ ]` of an open checklist item. It keeps its width (and stays
    /// clickable) but is drawn as an empty box; its text is transparent.
    checkbox,
    /// The `[x]` of a ticked checklist item, drawn as a ticked box.
    checkedBox,
    /// A `---` divider, drawn as a horizontal rule across the line.
    rule
};

/// `hidden`, `bullet`, `checkbox`, `checkedBox`, `rule`.
QString markupName(Markup markup);
std::optional<Markup> markupFromName(const QString &name);

/// The `Markup` a character format carries, if any.
std::optional<Markup> markupOf(const QTextCharFormat &format);

/// Restyles `document` (or just the paragraphs touching `range`).
///
/// An incremental restyle assumes the rest of the document was styled by an
/// earlier call. It continues past `range` while a code fence opened or
/// closed inside it changes how the following lines read.
///
/// With `concealsMarkup`, markup is marked with `markupProperty` instead of
/// being shown, except on the lines `revealing` touches (the caret's or
/// selection's paragraphs), and list indents follow the concealed markers.
///
/// Only the properties the styler sets are replaced; other character
/// properties (objects such as images) survive. Block formats are replaced.
void apply(QTextDocument *document, std::optional<TextRange> range, const BlueprintPalette &palette,
           double fontSize = defaultFontSize, bool concealsMarkup = false, std::optional<TextRange> revealing = std::nullopt);

/// A styled document of `markdown`, for read-only display and export.
std::unique_ptr<QTextDocument> attributedString(const QString &markdown, const BlueprintPalette &palette,
                                                double fontSize = defaultFontSize);

/// Styled text for reading rather than editing, as in PDF export. Like
/// `attributedString`, but with the markup taken out: bullets become a bullet
/// character, checkboxes `☐`/`☑`, fence lines become blank padding lines of the code
/// block, and dividers a blank line marked with `ruleProperty` for the caller
/// to draw a rule through.
std::unique_ptr<QTextDocument> presentation(const QString &markdown, const BlueprintPalette &palette,
                                            double fontSize = defaultFontSize);

// MARK: Reading styles back

/// The character format at UTF-16 `index` (the block format of an empty line's
/// terminator). Out-of-range indexes give the last character's.
QTextCharFormat formatAt(const QTextDocument &document, int index);

/// NSParagraphStyle-style readings of a block format.
inline double headIndent(const QTextBlockFormat &f)
{
    return f.leftMargin();
}
inline double firstLineHeadIndent(const QTextBlockFormat &f)
{
    return f.leftMargin() + f.textIndent();
}
/// Extra space between the lines of a paragraph.
inline double lineSpacing(const QTextBlockFormat &f)
{
    return f.lineHeightType() == QTextBlockFormat::LineDistanceHeight ? f.lineHeight() : 0;
}

// MARK: Concealed list markers

/// How a line's leading marker looks while concealed: which parts are
/// hidden or drawn differently (ranges within the marker), and the text that
/// stays visible, for the hanging indent.
struct ConcealedMarker {
    struct Part {
        TextRange range;
        Markup markup;
        bool operator==(const Part &) const = default;
    };

    QList<Part> parts;
    QString shownPrefix;

    ConcealedMarker(const MarkdownLine &line, const QString &prefix);
    bool operator==(const ConcealedMarker &) const = default;
};

} // namespace MarkdownStyler

} // namespace wp
