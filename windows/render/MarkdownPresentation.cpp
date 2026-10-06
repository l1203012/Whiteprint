#include "render/Fonts.h"
#include "render/MarkdownStyler.h"

#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <cmath>
#include <vector>

namespace wp {

namespace MarkdownStyler {

namespace {

struct Edit {
    TextRange range;
    QString replacement;
    /// Replaces (part of) the line's leading marker.
    bool isPrefix = false;
};

std::vector<Edit> presentationEdits(const MarkdownLine &line, const QString &contents)
{
    const int length = int(contents.size());
    int indent = 0;
    while (indent < length && MarkdownSyntax::isWhitespace(contents.at(indent).unicode()))
        ++indent;
    std::vector<Edit> edits;
    switch (line.kind) {
    case MarkdownLineKind::fence:
    case MarkdownLineKind::divider:
        return {Edit{TextRange(0, length), line.kind == MarkdownLineKind::divider ? QStringLiteral(" ") : QString(), false}};
    case MarkdownLineKind::code:
    case MarkdownLineKind::blank:
        return {};
    case MarkdownLineKind::heading:
    case MarkdownLineKind::quote:
        edits.push_back({TextRange(indent, line.markerLength - indent), QString(), true});
        break;
    case MarkdownLineKind::bullet:
        edits.push_back({TextRange(indent, 1), QString(QChar(0x2022)), true});
        break;
    case MarkdownLineKind::task:
        edits.push_back({TextRange(indent, line.markerLength - indent),
                         line.checked ? QString(QChar(0x2611)) + QChar(0x2002) : QString(QChar(0x2610)) + QChar(0x2002), true});
        break;
    case MarkdownLineKind::ordered:
    case MarkdownLineKind::paragraph:
        break;
    }
    const QString body = contents.mid(line.markerLength);
    for (const MarkdownSpan &span : MarkdownSyntax::spans(body, line.markerLength))
        for (const TextRange &markup : span.markupRanges())
            edits.push_back({markup, QString(), false});
    return edits;
}

/// Recomputes the hanging indent after the line's marker changed.
void reindent(QTextCursor &cursor, const QTextBlock &block, int prefixLength, const QFont &font)
{
    const QString text = block.text();
    if (text.isEmpty())
        return;
    const QString prefix = text.left(std::min(prefixLength, int(text.size())));
    QTextBlockFormat bf = block.blockFormat();
    const double first = firstLineHeadIndent(bf);
    const double head = first + std::ceil(QFontMetricsF(font).horizontalAdvance(prefix));
    bf.setLeftMargin(head);
    bf.setTextIndent(first - head);
    cursor.setPosition(block.position());
    cursor.setBlockFormat(bf);
}

} // namespace

std::unique_ptr<QTextDocument> presentation(const QString &markdown, const BlueprintPalette &palette, double fontSize)
{
    std::unique_ptr<QTextDocument> doc = attributedString(markdown, palette, fontSize);

    std::vector<MarkdownLine> lines;
    std::optional<MarkdownFenceState> fence;
    for (QTextBlock b = doc->firstBlock(); b.isValid(); b = b.next()) {
        const auto classified = MarkdownSyntax::classify(b.text(), fence);
        fence = classified.openFence;
        lines.push_back(classified.line);
    }

    const QFont prefixFont = fonts::system(fontSize);
    QTextCursor cursor(doc.get());
    cursor.beginEditBlock();
    for (int number = int(lines.size()) - 1; number >= 0; --number) {
        const MarkdownLine &line = lines[size_t(number)];
        const QTextBlock block = doc->findBlockByNumber(number);
        std::vector<Edit> edits = presentationEdits(line, block.text());
        std::vector<Edit> ordered = edits;
        std::stable_sort(ordered.begin(), ordered.end(), [](const Edit &a, const Edit &b) { return a.range.location > b.range.location; });
        const int base = block.position();
        for (const Edit &edit : ordered) {
            // Replacement text takes the attributes of the first replaced character.
            const QTextCharFormat format = edit.range.length > 0 ? formatAt(*doc, base + edit.range.location) : QTextCharFormat();
            cursor.setPosition(base + edit.range.location);
            cursor.setPosition(base + edit.range.end(), QTextCursor::KeepAnchor);
            if (edit.replacement.isEmpty())
                cursor.removeSelectedText();
            else
                cursor.insertText(edit.replacement, edit.range.length > 0 ? format : cursor.charFormat());
        }
        const QTextBlock newBlock = doc->findBlockByNumber(number);
        switch (line.kind) {
        case MarkdownLineKind::bullet:
        case MarkdownLineKind::ordered:
        case MarkdownLineKind::task:
        case MarkdownLineKind::quote: {
            int change = 0;
            for (const Edit &e : edits)
                if (e.isPrefix) {
                    change = int(e.replacement.size()) - e.range.length;
                    break;
                }
            reindent(cursor, newBlock, line.markerLength + change, prefixFont);
            break;
        }
        case MarkdownLineKind::divider: {
            QTextCharFormat mark;
            mark.setProperty(ruleProperty, true);
            cursor.setPosition(newBlock.position());
            cursor.setPosition(newBlock.position() + int(newBlock.text().size()), QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(mark);
            break;
        }
        default:
            break;
        }
    }
    cursor.endEditBlock();
    return doc;
}

} // namespace MarkdownStyler

} // namespace wp
