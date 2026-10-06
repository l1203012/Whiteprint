#include "render/MarkdownStyler.h"

#include "render/Fonts.h"

#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <algorithm>
#include <cmath>
#include <vector>

namespace wp {

namespace MarkdownStyler {

QString markupName(Markup markup)
{
    switch (markup) {
    case Markup::hidden: return QStringLiteral("hidden");
    case Markup::bullet: return QStringLiteral("bullet");
    case Markup::checkbox: return QStringLiteral("checkbox");
    case Markup::checkedBox: return QStringLiteral("checkedBox");
    case Markup::rule: return QStringLiteral("rule");
    }
    return {};
}

std::optional<Markup> markupFromName(const QString &name)
{
    for (Markup m : {Markup::hidden, Markup::bullet, Markup::checkbox, Markup::checkedBox, Markup::rule})
        if (markupName(m) == name)
            return m;
    return std::nullopt;
}

std::optional<Markup> markupOf(const QTextCharFormat &format)
{
    if (!format.hasProperty(markupProperty))
        return std::nullopt;
    return markupFromName(format.stringProperty(markupProperty));
}

namespace {

// MARK: Theme

/// A paragraph style, in NSParagraphStyle terms.
struct Para {
    double lineSpacing = 0;
    double firstLineHeadIndent = 0;
    double headIndent = 0;
    double paragraphSpacingBefore = 0;
    double paragraphSpacing = 0;

    QTextBlockFormat format() const
    {
        QTextBlockFormat f;
        f.setLineHeight(lineSpacing, QTextBlockFormat::LineDistanceHeight);
        f.setLeftMargin(headIndent);
        f.setTextIndent(firstLineHeadIndent - headIndent);
        f.setTopMargin(paragraphSpacingBefore);
        f.setBottomMargin(paragraphSpacing);
        return f;
    }
};

/// Fonts, colours and paragraph styles for one palette and body size.
struct Theme {
    BlueprintPalette palette;
    double fontSize;
    QFont body;
    QFont monospaced;
    Para bodyParagraph;
    /// Fence lines sit flush left so the block's background starts at the margin.
    Para fenceParagraph;
    Para codeParagraph;
    QColor codeBlockBackground;
    QColor inlineCodeBackground;
    double quoteIndent;

    Theme(const BlueprintPalette &palette, double fontSize) : palette(palette), fontSize(fontSize)
    {
        body = fonts::system(fontSize);
        monospaced = fonts::monospaced(std::round(fontSize * 0.87));
        bodyParagraph.lineSpacing = fontSize * 0.35;
        fenceParagraph.lineSpacing = fontSize * 0.2;
        codeParagraph.lineSpacing = fontSize * 0.2;
        codeParagraph.firstLineHeadIndent = fontSize * 0.75;
        codeParagraph.headIndent = fontSize * 0.75;
        codeBlockBackground = withAlpha(palette.text, 0.08);
        inlineCodeBackground = withAlpha(palette.text, 0.13);
        quoteIndent = fontSize * 0.8;
    }

    /// H1/H2/H3 scale with the body size (28/22/18 pt at 15 pt); H4-H6 are bold body text.
    QFont headingFont(int level) const
    {
        static const double scale[3] = {28, 22, 18};
        const double size = level <= 3 ? std::round(fontSize * scale[level - 1] / 15) : fontSize;
        return fonts::system(size, QFont::DemiBold);
    }

    Para headingParagraph(int level) const
    {
        Para p;
        p.lineSpacing = fontSize * 0.2;
        p.paragraphSpacingBefore = fontSize * (level == 1 ? 0.6 : level == 2 ? 0.5 : 0.35);
        p.paragraphSpacing = fontSize * 0.2;
        return p;
    }

    /// Wrapped lines start where the text after `prefix` (indent and marker) starts.
    Para hangingParagraph(const QString &prefix, double firstLineIndent) const
    {
        Para p;
        p.lineSpacing = fontSize * 0.35;
        p.firstLineHeadIndent = firstLineIndent;
        const double width = QFontMetricsF(body).horizontalAdvance(prefix);
        p.headIndent = firstLineIndent + std::ceil(width);
        return p;
    }

    QFont inlineCodeFont(double matchingSize) const { return fonts::monospaced(std::round(matchingSize * 0.87)); }

    QFont addingBold(const QFont &font) const
    {
        if (!fonts::isMonospaced(font)) {
            const bool italic = font.italic();
            QFont bold = fonts::system(fonts::sizeOf(font), QFont::Bold);
            bold.setStrikeOut(font.strikeOut());
            if (italic)
                bold.setItalic(true);
            return bold;
        }
        QFont f = font;
        f.setWeight(QFont::Bold);
        return f;
    }

    QFont addingItalic(const QFont &font) const
    {
        QFont f = font;
        f.setItalic(true);
        return f;
    }
};

// MARK: Per-character attributes

/// The styler-managed attributes of one character.
struct Attrs {
    QFont font;
    QColor foreground;
    QColor background; // invalid: none
    bool codeBlock = false;
    std::optional<QString> link;
    std::optional<Markup> markup;

    bool operator==(const Attrs &o) const
    {
        return font == o.font && foreground == o.foreground && background == o.background && codeBlock == o.codeBlock
               && link == o.link && markup == o.markup;
    }
};

struct LineStyle {
    Attrs base;
    Para para;
    std::vector<Attrs> chars;
};

template<class F>
void forRange(std::vector<Attrs> &chars, TextRange range, F &&f)
{
    const int start = std::max(0, range.location);
    const int end = std::min(int(chars.size()), range.end());
    for (int i = start; i < end; ++i)
        f(chars[size_t(i)]);
}

void styleInline(LineStyle &line, TextRange body, const QString &text, const Theme &theme, bool conceals)
{
    if (body.length <= 0)
        return;
    const QList<MarkdownSpan> spans = MarkdownSyntax::spans(text.mid(body.location, body.length), body.location);
    for (const MarkdownSpan &span : spans) {
        switch (span.kind) {
        case MarkdownSpan::Kind::code: {
            const double size = span.range.location < int(line.chars.size())
                                    ? fonts::sizeOf(line.chars[size_t(span.range.location)].font)
                                    : theme.fontSize;
            const QFont font = theme.inlineCodeFont(size);
            forRange(line.chars, span.range, [&](Attrs &a) {
                a.font = font;
                a.background = theme.inlineCodeBackground;
            });
            break;
        }
        case MarkdownSpan::Kind::link:
            forRange(line.chars, span.contentRange, [&](Attrs &a) {
                a.foreground = theme.palette.accent;
                if (span.url)
                    a.link = *span.url;
            });
            break;
        case MarkdownSpan::Kind::bold:
            forRange(line.chars, span.contentRange, [&](Attrs &a) { a.font = theme.addingBold(a.font); });
            break;
        case MarkdownSpan::Kind::italic:
            forRange(line.chars, span.contentRange, [&](Attrs &a) { a.font = theme.addingItalic(a.font); });
            break;
        }
    }
    for (const MarkdownSpan &span : spans) {
        for (const TextRange &markup : span.markupRanges()) {
            forRange(line.chars, markup, [&](Attrs &a) {
                a.foreground = theme.palette.muted;
                if (conceals)
                    a.markup = Markup::hidden;
            });
        }
    }
}

LineStyle styleLine(const MarkdownLine &line, const QString &text, const Theme &theme, bool conceals)
{
    const int n = int(text.size());
    const TextRange marker(0, std::min(line.markerLength, n));
    const TextRange body(marker.end(), n - marker.length);
    const QString prefix = text.left(marker.length);
    std::optional<ConcealedMarker> concealed;
    if (conceals)
        concealed = ConcealedMarker(line, prefix);
    const QString shownPrefix = concealed ? concealed->shownPrefix : prefix;

    LineStyle out;
    Attrs &base = out.base;
    base.foreground = theme.palette.text;
    base.font = theme.body;
    bool codeLine = false;

    switch (line.kind) {
    case MarkdownLineKind::fence:
    case MarkdownLineKind::code:
        base.font = theme.monospaced;
        base.foreground = line.kind == MarkdownLineKind::fence ? theme.palette.muted : theme.palette.text;
        base.background = theme.codeBlockBackground;
        base.codeBlock = true;
        out.para = line.kind == MarkdownLineKind::fence ? theme.fenceParagraph : theme.codeParagraph;
        codeLine = true;
        break;
    case MarkdownLineKind::heading:
        base.font = theme.headingFont(line.level);
        out.para = theme.headingParagraph(line.level);
        break;
    case MarkdownLineKind::bullet:
    case MarkdownLineKind::ordered:
    case MarkdownLineKind::task:
        out.para = theme.hangingParagraph(shownPrefix, 0);
        break;
    case MarkdownLineKind::quote:
        base.foreground = theme.palette.muted;
        out.para = theme.hangingParagraph(shownPrefix, theme.quoteIndent);
        break;
    case MarkdownLineKind::divider:
        base.foreground = theme.palette.muted;
        out.para = theme.bodyParagraph;
        break;
    case MarkdownLineKind::blank:
    case MarkdownLineKind::paragraph:
        out.para = theme.bodyParagraph;
        break;
    }
    out.chars.assign(size_t(n), base);

    if (codeLine) {
        if (conceals && line.kind == MarkdownLineKind::fence)
            forRange(out.chars, TextRange(0, n), [](Attrs &a) { a.markup = Markup::hidden; });
        return out;
    }
    if (line.kind == MarkdownLineKind::divider) {
        if (conceals)
            forRange(out.chars, TextRange(0, n), [](Attrs &a) { a.markup = Markup::rule; });
        return out;
    }

    styleInline(out, body, text, theme, conceals);
    forRange(out.chars, marker, [&](Attrs &a) { a.foreground = theme.palette.muted; });
    if (concealed) {
        for (const auto &part : concealed->parts) {
            const TextRange range(marker.location + part.range.location, part.range.length);
            forRange(out.chars, range, [&](Attrs &a) {
                a.markup = part.markup;
                if (part.markup == Markup::checkbox || part.markup == Markup::checkedBox)
                    a.foreground = QColor(Qt::transparent);
            });
        }
    }
    if (line.kind == MarkdownLineKind::task) {
        const int box = int(prefix.indexOf(QLatin1Char('[')));
        if (line.checked && box >= 0 && !conceals)
            forRange(out.chars, TextRange(marker.location + box, 3), [&](Attrs &a) { a.foreground = theme.palette.accent; });
        if (line.checked && body.length > 0) {
            forRange(out.chars, body, [&](Attrs &a) {
                a.foreground = theme.palette.muted;
                a.font.setStrikeOut(true);
            });
        }
    }
    return out;
}

// MARK: Writing into the document

void applyAttrs(QTextCharFormat &f, const Attrs &a)
{
    f.setFont(a.font);
    f.setForeground(QBrush(a.foreground));
    if (a.background.isValid())
        f.setBackground(QBrush(a.background));
    else
        f.clearBackground();
    if (a.codeBlock)
        f.setProperty(codeBlockProperty, true);
    else
        f.clearProperty(codeBlockProperty);
    if (a.link)
        f.setProperty(linkProperty, *a.link);
    else
        f.clearProperty(linkProperty);
    if (a.markup)
        f.setProperty(markupProperty, markupName(*a.markup));
    else
        f.clearProperty(markupProperty);
}

std::optional<MarkdownFenceState> storedFence(const QTextBlock &block)
{
    const QString encoded = block.blockFormat().stringProperty(fenceProperty);
    return MarkdownFenceState::fromEncoded(encoded);
}

/// The fence left open after `previous`.
std::optional<MarkdownFenceState> fenceAfter(const QTextBlock &previous)
{
    return MarkdownSyntax::classify(previous.text(), storedFence(previous)).openFence;
}

void writeBlock(QTextCursor &cursor, const QTextBlock &block, const LineStyle &style, const std::optional<MarkdownFenceState> &fence)
{
    const int start = block.position();
    QTextBlockFormat bf = style.para.format();
    if (fence)
        bf.setProperty(fenceProperty, fence->encoded());

    // Character runs first, split at the existing fragments so their other properties survive.
    struct Run {
        int from, to;
        QTextCharFormat format;
    };
    std::vector<Run> runs;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid())
            continue;
        const int from = fragment.position() - start;
        const int to = from + fragment.length();
        for (int i = from; i < to && i < int(style.chars.size());) {
            int j = i + 1;
            while (j < to && j < int(style.chars.size()) && style.chars[size_t(j)] == style.chars[size_t(i)])
                ++j;
            QTextCharFormat f = fragment.charFormat();
            applyAttrs(f, style.chars[size_t(i)]);
            if (f != fragment.charFormat())
                runs.push_back({i, j, f});
            i = j;
        }
    }
    for (const Run &run : runs) {
        cursor.setPosition(start + run.from);
        cursor.setPosition(start + run.to, QTextCursor::KeepAnchor);
        cursor.setCharFormat(run.format);
    }

    cursor.setPosition(start);
    if (cursor.blockFormat() != bf)
        cursor.setBlockFormat(bf);
    QTextCharFormat blockChar = block.charFormat();
    applyAttrs(blockChar, style.base);
    if (blockChar != block.charFormat())
        cursor.setBlockCharFormat(blockChar);
}

/// Which lines hide their markup: all but those touching `revealing`.
struct Concealment {
    std::optional<TextRange> revealing;

    /// `line` includes its line break, `contents` doesn't.
    bool conceals(TextRange line, TextRange contents, int textLength) const
    {
        if (!revealing)
            return true;
        const int lo = std::max(line.location, revealing->location);
        const int hi = std::min(line.end(), revealing->end());
        if (hi - lo > 0 || line.contains(revealing->location))
            return false;
        // A caret at the very end of the text is on the last line, unless that ends in a line break.
        return !(revealing->location == textLength && contents.end() == textLength);
    }
};

void restyle(QTextDocument *doc, std::optional<TextRange> range, const Theme &theme, const std::optional<Concealment> &concealment)
{
    const int length = std::max(0, doc->characterCount() - 1);
    QTextBlock start = doc->firstBlock();
    int lastDirty = doc->lastBlock().position();
    if (range) {
        const int location = std::min(std::max(range->location, 0), length);
        const int span = std::min(range->length, length - location);
        start = doc->findBlock(location);
        lastDirty = doc->findBlock(location + std::max(0, span)).position();
    }
    if (!start.isValid())
        return;

    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    std::optional<MarkdownFenceState> fence;
    if (start.position() > 0 && start.previous().isValid())
        fence = fenceAfter(start.previous());
    for (QTextBlock block = start; block.isValid(); block = block.next()) {
        if (block.position() > lastDirty && storedFence(block) == fence)
            break;
        const QString text = block.text();
        const int blockStart = block.position();
        const TextRange contents(blockStart, int(text.size()));
        const TextRange lineRange(blockStart, std::min(blockStart + block.length(), length) - blockStart);
        const auto classified = MarkdownSyntax::classify(text, fence);
        const bool conceals = concealment ? concealment->conceals(lineRange, contents, length) : false;
        const LineStyle style = styleLine(classified.line, text, theme, conceals);
        writeBlock(cursor, block, style, fence);
        fence = classified.openFence;
    }
    cursor.endEditBlock();
}

} // namespace

// MARK: Public

void apply(QTextDocument *document, std::optional<TextRange> range, const BlueprintPalette &palette, double fontSize,
           bool concealsMarkup, std::optional<TextRange> revealing)
{
    if (!document)
        return;
    std::optional<Concealment> concealment;
    if (concealsMarkup)
        concealment = Concealment{revealing};
    restyle(document, range, Theme(palette, fontSize), concealment);
}

std::unique_ptr<QTextDocument> attributedString(const QString &markdown, const BlueprintPalette &palette, double fontSize)
{
    auto doc = std::make_unique<QTextDocument>();
    doc->setUndoRedoEnabled(false);
    doc->setDocumentMargin(0);
    doc->setPlainText(markdown);
    restyle(doc.get(), std::nullopt, Theme(palette, fontSize), std::nullopt);
    // The fence context is only for incremental restyles.
    QTextCursor cursor(doc.get());
    cursor.beginEditBlock();
    for (QTextBlock b = doc->firstBlock(); b.isValid(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();
        if (bf.hasProperty(fenceProperty)) {
            bf.clearProperty(fenceProperty);
            cursor.setPosition(b.position());
            cursor.setBlockFormat(bf);
        }
    }
    cursor.endEditBlock();
    return doc;
}

QTextCharFormat formatAt(const QTextDocument &document, int index)
{
    const int length = std::max(0, document.characterCount() - 1);
    const int i = std::min(std::max(index, 0), std::max(0, length - 1));
    const QTextBlock block = document.findBlock(i);
    if (!block.isValid())
        return {};
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment f = it.fragment();
        if (f.isValid() && i >= f.position() && i < f.position() + f.length())
            return f.charFormat();
    }
    return block.charFormat();
}

// MARK: Concealed markers

ConcealedMarker::ConcealedMarker(const MarkdownLine &line, const QString &prefix) : shownPrefix(prefix)
{
    const int count = int(prefix.size());
    int indent = 0;
    while (indent < count && MarkdownSyntax::isWhitespace(prefix.at(indent).unicode()))
        ++indent;
    const QString indentText = prefix.left(indent);
    switch (line.kind) {
    case MarkdownLineKind::heading:
        if (count > 0)
            parts = {Part{TextRange(0, count), Markup::hidden}};
        shownPrefix = QString();
        break;
    case MarkdownLineKind::quote:
        if (indent < count)
            parts = {Part{TextRange(indent, count - indent), Markup::hidden}};
        shownPrefix = indentText;
        break;
    case MarkdownLineKind::bullet:
        if (indent >= count)
            break;
        parts = {Part{TextRange(indent, 1), Markup::bullet}};
        shownPrefix = indentText + QChar(0x2022) + prefix.mid(indent + 1);
        break;
    case MarkdownLineKind::task: {
        const int box = int(prefix.indexOf(QLatin1Char('[')));
        if (box < 0 || box + 3 > count)
            break;
        parts = {Part{TextRange(indent, box - indent), Markup::hidden},
                 Part{TextRange(box, 3), line.checked ? Markup::checkedBox : Markup::checkbox}};
        shownPrefix = indentText + prefix.mid(box);
        break;
    }
    default:
        break;
    }
}

} // namespace MarkdownStyler

} // namespace wp
