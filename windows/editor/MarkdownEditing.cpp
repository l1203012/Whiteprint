#include "editor/MarkdownEditing.h"

#include "core/TextUtil.h"

#include <algorithm>

namespace wp {

namespace MarkdownEditing {

namespace {

struct Renumbered {
    TextLine line;
    QString text;
};

/// The ordered siblings after `line` at the same indent, renumbered from
/// `first`, plus any nested lines between them (unchanged).
QList<Renumbered> renumberedItems(const TextLine &line, const QString &text, const QString &indent, int first)
{
    QList<Renumbered> result;
    int number = first;
    int location = line.end() + 1;
    const int length = int(text.size());
    while (location <= length && line.end() < length) {
        const TextLine next = TextLine::at(location, text);
        const MarkdownLinePrefix parsed(next.text);
        if (parsed.kind.type == LineKind::Type::ordered && parsed.indent == indent) {
            const QString content = next.text.mid(parsed.prefixLength);
            result.append({next, indent + QString::number(number) + parsed.kind.mark + QLatin1Char(' ') + content});
            ++number;
        } else if (parsed.indent.size() > indent.size() && !isAllWhitespace(next.text)) {
            result.append({next, next.text});
        } else {
            break;
        }
        if (next.end() >= length)
            break;
        location = next.end() + 1;
    }
    // Trailing nested lines after the last renumbered item don't need rewriting.
    while (!result.isEmpty() && result.last().line.text == result.last().text)
        result.removeLast();
    return result;
}

QString removingIndentLevel(const QString &indent)
{
    if (indent.startsWith(QLatin1Char('\t')))
        return indent.mid(1);
    int spaces = 0;
    while (spaces < indent.size() && indent[spaces] == QLatin1Char(' '))
        ++spaces;
    return indent.mid(std::min(spaces, int(indentUnit.size())));
}

} // namespace

std::optional<int> orderedNumber(const TextLine &before, const QString &text, const QString &indent)
{
    int location = before.start() - 1;
    while (location >= 0) {
        const TextLine previous = TextLine::at(location, text);
        const MarkdownLinePrefix parsed(previous.text);
        if (isAllWhitespace(previous.text) || parsed.indent.size() < indent.size())
            return std::nullopt;
        if (parsed.indent == indent) {
            if (parsed.kind.type == LineKind::Type::ordered)
                return parsed.kind.number + 1;
            return std::nullopt;
        }
        location = previous.start() - 1;
    }
    return std::nullopt;
}

std::optional<TextChange> newline(const QString &text, TextRange selection)
{
    if (selection.length != 0)
        return std::nullopt;
    const int caret = selection.location;
    const TextLine line = TextLine::at(caret, text);
    if (CodeFence::isCode(line.start(), text))
        return std::nullopt;

    if (const auto fence = CodeFence::opener(line.text); fence && caret == line.end() && CodeFence::isUnclosed(line.start(), text)) {
        const QString close = QString(fence->length, fence->marker);
        return TextChange{TextRange(caret, 0), QStringLiteral("\n\n") + close, TextRange(caret + 1, 0)};
    }

    const MarkdownLinePrefix parsed(line.text);
    const auto marker = parsed.continuationMarker();
    if (!parsed.isListItem() || !marker || caret - line.start() < parsed.prefixLength)
        return std::nullopt;
    const QString content = line.text.mid(parsed.prefixLength);

    if (trimmedWhitespace(content).isEmpty()) {
        if (!parsed.indent.isEmpty())
            return indent(text, selection, true);
        return TextChange{line.range, QString(), TextRange(line.start(), 0)};
    }

    QString replacement = QStringLiteral("\n") + parsed.indent + *marker;
    const int newCaret = caret + int(replacement.size());
    int end = caret;
    if (parsed.kind.type == LineKind::Type::ordered) {
        const QList<Renumbered> renumbered = renumberedItems(line, text, parsed.indent, parsed.kind.number + 2);
        if (!renumbered.isEmpty()) {
            replacement += text.mid(caret, line.end() - caret);
            for (const Renumbered &item : renumbered)
                replacement += QLatin1Char('\n') + item.text;
            end = renumbered.last().line.end();
        }
    }
    return TextChange{TextRange(caret, end - caret), replacement, TextRange(newCaret, 0)};
}

std::optional<TextChange> indent(const QString &text, TextRange selection, bool outdent)
{
    const int end = selection.end();
    QList<TextLine> lines;
    for (const TextLine &line : TextLine::all(text)) {
        if (line.end() >= selection.location && (line.start() < end || line.start() == selection.location))
            lines.append(line);
    }
    if (lines.isEmpty())
        return std::nullopt;
    const TextLine first = lines.first();
    const TextLine last = lines.last();
    bool anyList = false;
    for (const TextLine &line : lines)
        anyList = anyList || MarkdownLinePrefix(line.text).isListItem();
    if (!anyList || CodeFence::isCode(first.start(), text))
        return std::nullopt;

    QStringList newLines;
    QStringList oldLines;
    int delta = 0, startDelta = 0;
    for (const TextLine &line : lines) {
        MarkdownLinePrefix parsed(line.text);
        QString newText = line.text;
        if (parsed.isListItem()) {
            QString indentText = parsed.indent;
            if (outdent)
                indentText = removingIndentLevel(indentText);
            else
                indentText += indentUnit;
            const QString content = line.text.mid(parsed.indent.size());
            newText = indentText + content;
            if (parsed.kind.type == LineKind::Type::ordered) {
                const QChar delimiter = parsed.kind.mark;
                const int number = orderedNumber(line, text, indentText).value_or(1);
                const MarkdownLinePrefix reparsed(newText);
                const QString rest = newText.mid(reparsed.prefixLength);
                newText = indentText + QString::number(number) + delimiter + QLatin1Char(' ') + rest;
            }
        }
        const int lineDelta = int(newText.size() - line.text.size());
        if (line == first)
            startDelta = lineDelta;
        delta += lineDelta;
        newLines.append(newText);
        oldLines.append(line.text);
    }
    if (delta == 0 && newLines == oldLines)
        return std::nullopt;
    const TextRange range(first.start(), last.end() - first.start());
    const QString replacement = newLines.join(QLatin1Char('\n'));
    TextRange newSelection;
    if (selection.length == 0)
        newSelection = TextRange(std::max(first.start(), selection.location + startDelta), 0);
    else
        newSelection = TextRange(first.start(), int(replacement.size()));
    return TextChange{range, replacement, newSelection};
}

std::optional<TextChange> shortcut(const QString &inserting, TextRange range, const QString &text)
{
    if (inserting != QLatin1String(" ") || range.length != 0)
        return std::nullopt;
    const TextLine line = TextLine::at(range.location, text);
    if (CodeFence::isCode(line.start(), text))
        return std::nullopt;
    const QString typed = text.mid(line.start(), range.location - line.start());
    int indentLength = 0;
    while (indentLength < typed.size() && (typed[indentLength] == QLatin1Char(' ') || typed[indentLength] == QLatin1Char('\t')))
        ++indentLength;
    const QString rest = typed.mid(indentLength);
    QString marker;
    if (rest == QLatin1String("[]") || rest == QLatin1String("[ ]"))
        marker = QStringLiteral("- [ ] ");
    else if (rest == QLatin1String("[x]") || rest == QLatin1String("[X]"))
        marker = QStringLiteral("- [x] ");
    else
        return std::nullopt;
    const int start = line.start() + indentLength;
    const TextRange changed(start, range.location - start);
    return TextChange{changed, marker, TextRange(start + int(marker.size()), 0)};
}

std::optional<TextRange> checkboxRange(int location, const QString &text)
{
    const TextLine line = TextLine::at(location, text);
    if (CodeFence::isCode(line.start(), text))
        return std::nullopt;
    const auto box = MarkdownLinePrefix(line.text).checkboxRange();
    if (!box)
        return std::nullopt;
    return TextRange(line.start() + box->location, box->length);
}

std::optional<TextChange> toggleCheckbox(int location, const QString &text, TextRange selection)
{
    const auto box = checkboxRange(location, text);
    if (!box)
        return std::nullopt;
    const bool checked = text.mid(box->location, box->length) != QLatin1String("[ ]");
    return TextChange{*box, checked ? QStringLiteral("[ ]") : QStringLiteral("[x]"), selection};
}

SlashEffect apply(SlashCommand command, const QString &text, TextRange slash)
{
    using Kind = SlashEffect::Kind;
    const TextChange removal{slash, QString(), TextRange(slash.location, 0)};
    const TextLine line = TextLine::at(slash.location, text);
    QString lineText = line.text;
    lineText.replace(slash.location - line.start(), std::min(slash.length, line.end() - slash.location), QString());
    const int caretInLine = slash.location - line.start();
    const MarkdownLinePrefix parsed(lineText);
    const QString content = lineText.mid(parsed.prefixLength);
    const int caretInContent = std::max(0, caretInLine - parsed.prefixLength);

    auto rewrite = [&](const QString &prefix) {
        const QString newLine = prefix + content;
        const int caret = line.start() + int(prefix.size()) + caretInContent;
        return SlashEffect{Kind::text, TextChange{line.range, newLine, TextRange(caret, 0)}};
    };

    switch (command) {
    case SlashCommand::heading1: return rewrite(QStringLiteral("# "));
    case SlashCommand::heading2: return rewrite(QStringLiteral("## "));
    case SlashCommand::heading3: return rewrite(QStringLiteral("### "));
    case SlashCommand::bulletList: return rewrite(parsed.indent + QStringLiteral("- "));
    case SlashCommand::numberedList: {
        const int number = orderedNumber(line, text, parsed.indent).value_or(1);
        return rewrite(parsed.indent + QString::number(number) + QStringLiteral(". "));
    }
    case SlashCommand::checklist: return rewrite(parsed.indent + QStringLiteral("- [ ] "));
    case SlashCommand::quote: return rewrite(QStringLiteral("> "));
    case SlashCommand::codeBlock: {
        const int caret = line.start() + 4 + (content.isEmpty() ? 0 : caretInContent);
        return SlashEffect{Kind::text, TextChange{line.range, QStringLiteral("```\n") + content + QStringLiteral("\n```"), TextRange(caret, 0)}};
    }
    case SlashCommand::divider: {
        if (trimmedWhitespace(content).isEmpty()) {
            const bool previousIsText = line.start() > 0 && !isAllWhitespace(TextLine::at(line.start() - 1, text).text);
            const QString divider = (previousIsText ? QStringLiteral("\n") : QString()) + QStringLiteral("---\n");
            return SlashEffect{Kind::text, TextChange{line.range, divider, TextRange(line.start() + int(divider.size()), 0)}};
        }
        const QString replacement = lineText + QStringLiteral("\n\n---\n");
        return SlashEffect{Kind::text, TextChange{line.range, replacement, TextRange(line.start() + int(replacement.size()), 0)}};
    }
    case SlashCommand::drawing: return SlashEffect{Kind::insertDrawing, removal};
    case SlashCommand::flashcards: return SlashEffect{Kind::insertDeck, removal};
    case SlashCommand::newPage: return SlashEffect{Kind::newPage, removal};
    }
    return SlashEffect{Kind::text, removal};
}

bool opensSlashMenu(int location, const QString &text)
{
    if (location > 0) {
        const QChar previous = text[location - 1];
        if (previous != QLatin1Char(' ') && previous != QLatin1Char('\n') && previous != QLatin1Char('\t'))
            return false;
    }
    return !CodeFence::isCode(TextLine::at(location, text).start(), text);
}

} // namespace MarkdownEditing

namespace CaretTransform {

TextRange transform(TextRange selection, const QString &a, const QString &b)
{
    const int an = int(a.size()), bn = int(b.size());
    int prefix = 0;
    while (prefix < an && prefix < bn && a[prefix] == b[prefix])
        ++prefix;
    int suffix = 0;
    while (suffix < an - prefix && suffix < bn - prefix && a[an - 1 - suffix] == b[bn - 1 - suffix])
        ++suffix;
    auto map = [&](int x) {
        if (x <= prefix)
            return std::min(x, bn);
        if (x >= an - suffix)
            return x - an + bn;
        return bn - suffix;
    };
    const int start = map(selection.location);
    const int end = map(selection.end());
    return TextRange(start, std::max(0, end - start));
}

} // namespace CaretTransform

} // namespace wp
