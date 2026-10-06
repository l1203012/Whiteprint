#include "editor/EditorCommand.h"

#include <algorithm>

namespace wp {

namespace EditorCommands {

QList<EditorCommand> all()
{
    return {EditorCommand::heading1,    EditorCommand::heading2,   EditorCommand::heading3,   EditorCommand::body,
            EditorCommand::bold,        EditorCommand::italic,     EditorCommand::inlineCode, EditorCommand::bulletList,
            EditorCommand::numberedList, EditorCommand::checklist, EditorCommand::quote,      EditorCommand::codeBlock,
            EditorCommand::divider,     EditorCommand::drawing,    EditorCommand::flashcards, EditorCommand::newPage};
}

QString rawValue(EditorCommand command)
{
    switch (command) {
    case EditorCommand::heading1: return QStringLiteral("heading1");
    case EditorCommand::heading2: return QStringLiteral("heading2");
    case EditorCommand::heading3: return QStringLiteral("heading3");
    case EditorCommand::body: return QStringLiteral("body");
    case EditorCommand::bold: return QStringLiteral("bold");
    case EditorCommand::italic: return QStringLiteral("italic");
    case EditorCommand::inlineCode: return QStringLiteral("inlineCode");
    case EditorCommand::bulletList: return QStringLiteral("bulletList");
    case EditorCommand::numberedList: return QStringLiteral("numberedList");
    case EditorCommand::checklist: return QStringLiteral("checklist");
    case EditorCommand::quote: return QStringLiteral("quote");
    case EditorCommand::codeBlock: return QStringLiteral("codeBlock");
    case EditorCommand::divider: return QStringLiteral("divider");
    case EditorCommand::drawing: return QStringLiteral("drawing");
    case EditorCommand::flashcards: return QStringLiteral("flashcards");
    case EditorCommand::newPage: return QStringLiteral("newPage");
    }
    return {};
}

std::optional<EditorCommand> fromRawValue(const QString &raw)
{
    for (EditorCommand command : all()) {
        if (rawValue(command) == raw)
            return command;
    }
    return std::nullopt;
}

QString title(EditorCommand command)
{
    switch (command) {
    case EditorCommand::heading1: return QStringLiteral("Heading 1");
    case EditorCommand::heading2: return QStringLiteral("Heading 2");
    case EditorCommand::heading3: return QStringLiteral("Heading 3");
    case EditorCommand::body: return QStringLiteral("Body");
    case EditorCommand::bold: return QStringLiteral("Bold");
    case EditorCommand::italic: return QStringLiteral("Italic");
    case EditorCommand::inlineCode: return QStringLiteral("Code");
    case EditorCommand::bulletList: return QStringLiteral("Bulleted List");
    case EditorCommand::numberedList: return QStringLiteral("Numbered List");
    case EditorCommand::checklist: return QStringLiteral("Checklist");
    case EditorCommand::quote: return QStringLiteral("Quote");
    case EditorCommand::codeBlock: return QStringLiteral("Code Block");
    case EditorCommand::divider: return QStringLiteral("Divider");
    case EditorCommand::drawing: return QStringLiteral("Insert Drawing");
    case EditorCommand::flashcards: return QStringLiteral("Insert Flashcards");
    case EditorCommand::newPage: return QStringLiteral("New Page");
    }
    return {};
}

QString glyph(EditorCommand command)
{
    switch (command) {
    case EditorCommand::heading1: return QStringLiteral("H1");
    case EditorCommand::heading2: return QStringLiteral("H2");
    case EditorCommand::heading3: return QStringLiteral("H3");
    case EditorCommand::body: return QStringLiteral("Body");
    case EditorCommand::bold: return QStringLiteral("B");
    case EditorCommand::italic: return QStringLiteral("I");
    case EditorCommand::inlineCode: return QStringLiteral("</>");
    case EditorCommand::bulletList: return QString(QChar(0x2022));
    case EditorCommand::numberedList: return QStringLiteral("1.");
    case EditorCommand::checklist: return QString(QChar(0x2611));
    case EditorCommand::quote: return QString(QChar(0x201C));
    case EditorCommand::codeBlock: return QStringLiteral("{}");
    case EditorCommand::divider: return QString(QChar(0x2014));
    case EditorCommand::drawing: return QString(QChar(0x25A1));
    case EditorCommand::flashcards: return QString(QChar(0x25A4));
    case EditorCommand::newPage: return QStringLiteral("+");
    }
    return {};
}

bool isTextCommand(EditorCommand command)
{
    return command != EditorCommand::drawing && command != EditorCommand::flashcards && command != EditorCommand::newPage;
}

} // namespace EditorCommands

namespace MarkdownFormatting {

namespace {

TextRange trimmingSpaces(TextRange range, const QString &text)
{
    int start = range.location, end = range.end();
    while (start < end && text[start] == QLatin1Char(' '))
        ++start;
    while (end > start && text[end - 1] == QLatin1Char(' '))
        --end;
    return TextRange(start, end - start);
}

/// `marker` at `index`, not part of a longer run of its character
/// (a `***` run counts as both bold and italic).
bool isDelimiter(const QString &marker, const QString &text, int index)
{
    const int m = int(marker.size());
    if (index < 0 || index + m > text.size() || text.mid(index, m) != marker)
        return false;
    const QChar character = text[index];
    int start = index, end = index + m;
    while (start > 0 && text[start - 1] == character)
        --start;
    while (end < text.size() && text[end] == character)
        ++end;
    const int run = end - start;
    return run == m || (character == QLatin1Char('*') && run == 3);
}

/// Whether `range` sits right between an opening and a closing `marker`
/// (and not inside a longer run, e.g. `_` isn't wrapped by `**`).
bool isWrapped(TextRange range, const QString &marker, const QString &text)
{
    const int m = int(marker.size());
    if (range.location < m || range.end() + m > text.size())
        return false;
    return isDelimiter(marker, text, range.location - m) && isDelimiter(marker, text, range.end());
}

std::optional<TextChange> wrapLines(const QString &marker, const QString &text, TextRange selection)
{
    QList<TextLine> lines;
    for (const TextLine &line : TextLine::all(text)) {
        const int overlap = std::min(line.end(), selection.end()) - std::max(line.start(), selection.location);
        const bool inside = line.range.length == 0 && line.start() >= selection.location && line.start() < selection.end();
        if (overlap > 0 || inside)
            lines.append(line);
    }
    if (lines.isEmpty())
        return std::nullopt;
    const TextLine first = lines.first(), last = lines.last();
    QList<TextRange> parts;
    QList<TextRange> wrapped;
    for (const TextLine &line : lines) {
        const int start = std::max(line.start(), selection.location);
        const int end = std::min(line.end(), selection.end());
        const TextRange part = trimmingSpaces(TextRange(start, std::max(0, end - start)), text);
        parts.append(part);
        if (part.length > 0)
            wrapped.append(part);
    }
    if (wrapped.isEmpty())
        return std::nullopt;
    const int m = int(marker.size());
    // The part with its markers, whether they're just outside or just inside it.
    auto markedRange = [&](TextRange part) -> std::optional<TextRange> {
        if (isWrapped(part, marker, text))
            return TextRange(part.location - m, part.length + 2 * m);
        if (part.length >= 2 * m && isDelimiter(marker, text, part.location) && isDelimiter(marker, text, part.end() - m))
            return part;
        return std::nullopt;
    };
    const bool unwrap = std::all_of(wrapped.begin(), wrapped.end(), [&](TextRange r) { return markedRange(r).has_value(); });
    QStringList lineTexts;
    for (int i = 0; i < lines.size(); ++i) {
        const TextLine &line = lines[i];
        const TextRange part = parts[i];
        QString lineText = line.text;
        if (part.length > 0) {
            const TextRange local(part.location - line.start(), part.length);
            const auto marked = markedRange(part);
            if (unwrap && marked) {
                const TextRange outer(marked->location - line.start(), marked->length);
                const TextRange inner(outer.location + m, outer.length - 2 * m);
                lineText.replace(outer.location, outer.length, lineText.mid(inner.location, inner.length));
            } else {
                lineText.replace(local.location, local.length, marker + lineText.mid(local.location, local.length) + marker);
            }
        }
        lineTexts.append(lineText);
    }
    const TextRange range(first.start(), last.end() - first.start());
    const QString replacement = lineTexts.join(QLatin1Char('\n'));
    return TextChange{range, replacement, TextRange(range.location, int(replacement.size()))};
}

QString markerFor(const LineKind &kind, int number)
{
    switch (kind.type) {
    case LineKind::Type::paragraph: return QString();
    case LineKind::Type::heading: return QString(kind.level, QLatin1Char('#')) + QLatin1Char(' ');
    case LineKind::Type::bullet: return QString(kind.mark) + QLatin1Char(' ');
    case LineKind::Type::ordered: return QString::number(number) + kind.mark + QLatin1Char(' ');
    case LineKind::Type::checklist:
        return QString(kind.mark) + QStringLiteral(" [") + (kind.checked ? QStringLiteral("x") : QStringLiteral(" ")) + QStringLiteral("] ");
    case LineKind::Type::quote: return QStringLiteral("> ");
    }
    return {};
}

bool isList(const LineKind &kind)
{
    return kind.type == LineKind::Type::bullet || kind.type == LineKind::Type::ordered || kind.type == LineKind::Type::checklist;
}

/// Same kind of line, ignoring numbers, bullets and ticks.
bool sameKind(const LineKind &a, const LineKind &b)
{
    if (a.type != b.type)
        return false;
    return a.type != LineKind::Type::heading || a.level == b.level;
}

/// Fences the selected lines as a code block; on an empty line, inserts
/// an empty block with the caret inside.
std::optional<TextChange> codeBlock(const QString &text, TextRange selection)
{
    const TextLine firstLine = TextLine::at(selection.location, text);
    if (CodeFence::isCode(firstLine.start(), text))
        return std::nullopt;
    const TextLine lastLine = TextLine::at(selection.end(), text);
    const TextRange range(firstLine.start(), lastLine.end() - firstLine.start());
    const QString body = text.mid(range.location, range.length);
    const QString replacement = QStringLiteral("```\n") + body + QStringLiteral("\n```");
    const int caret = selection.length == 0 ? selection.location - firstLine.start() : 0;
    const TextRange newSelection = selection.length == 0 ? TextRange(firstLine.start() + 4 + caret, 0)
                                                         : TextRange(firstLine.start() + 4, int(body.size()));
    return TextChange{range, replacement, newSelection};
}

} // namespace

std::optional<TextChange> apply(EditorCommand command, const QString &text, TextRange selection)
{
    switch (command) {
    case EditorCommand::bold: return toggleWrap(QStringLiteral("**"), text, selection);
    case EditorCommand::italic: return toggleWrap(QStringLiteral("_"), text, selection);
    case EditorCommand::inlineCode: return toggleWrap(QStringLiteral("`"), text, selection);
    case EditorCommand::heading1: return setLineKind(LineKind::heading(1), text, selection);
    case EditorCommand::heading2: return setLineKind(LineKind::heading(2), text, selection);
    case EditorCommand::heading3: return setLineKind(LineKind::heading(3), text, selection);
    case EditorCommand::body: return setLineKind(LineKind::paragraph(), text, selection);
    case EditorCommand::bulletList: return setLineKind(LineKind::bullet(QLatin1Char('-')), text, selection);
    case EditorCommand::numberedList: return setLineKind(LineKind::ordered(1, QLatin1Char('.')), text, selection);
    case EditorCommand::checklist: return setLineKind(LineKind::checklist(false, QLatin1Char('-')), text, selection);
    case EditorCommand::quote: return setLineKind(LineKind::quote(), text, selection);
    case EditorCommand::codeBlock: return codeBlock(text, selection);
    case EditorCommand::divider: {
        const MarkdownEditing::SlashEffect effect = MarkdownEditing::apply(SlashCommand::divider, text, TextRange(selection.end(), 0));
        if (effect.kind != MarkdownEditing::SlashEffect::Kind::text)
            return std::nullopt;
        return effect.change;
    }
    case EditorCommand::drawing:
    case EditorCommand::flashcards:
    case EditorCommand::newPage: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<TextChange> toggleWrap(const QString &marker, const QString &text, TextRange selection)
{
    const int m = int(marker.size());
    if (CodeFence::isCode(TextLine::at(selection.location, text).start(), text))
        return std::nullopt;
    if (selection.length == 0) {
        const int caret = selection.location;
        if (caret >= m && caret + m <= text.size() && text.mid(caret - m, 2 * m) == marker + marker)
            return TextChange{TextRange(caret - m, 2 * m), QString(), TextRange(caret - m, 0)};
        return TextChange{selection, marker + marker, TextRange(caret + m, 0)};
    }
    const QString selected = text.mid(selection.location, selection.length);
    if (selected.contains(QLatin1Char('\n')))
        return wrapLines(marker, text, selection);
    if (isWrapped(selection, marker, text)) {
        const TextRange outer(selection.location - m, selection.length + 2 * m);
        return TextChange{outer, selected, TextRange(outer.location, selection.length)};
    }
    if (selection.length >= 2 * m && selected.startsWith(marker) && selected.endsWith(marker) && isDelimiter(marker, selected, 0)
        && isDelimiter(marker, selected, selection.length - m)) {
        const QString inner = selected.mid(m, selection.length - 2 * m);
        return TextChange{selection, inner, TextRange(selection.location, int(inner.size()))};
    }
    const TextRange trimmed = trimmingSpaces(selection, text);
    if (trimmed.length <= 0)
        return std::nullopt;
    return TextChange{trimmed, marker + text.mid(trimmed.location, trimmed.length) + marker,
                      TextRange(trimmed.location + m, trimmed.length)};
}

std::optional<TextChange> setLineKind(const LineKind &kind, const QString &text, TextRange selection)
{
    const int end = selection.end();
    QList<TextLine> lines;
    for (const TextLine &line : TextLine::all(text)) {
        if (line.end() >= selection.location && (line.start() < end || line.start() == selection.location))
            lines.append(line);
    }
    if (lines.isEmpty())
        return std::nullopt;
    const TextLine first = lines.first(), last = lines.last();
    QList<TextLine> editable;
    for (const TextLine &line : lines) {
        if (!CodeFence::isCode(line.start(), text) && !(lines.size() > 1 && isAllWhitespace(line.text)))
            editable.append(line);
    }
    if (editable.isEmpty())
        return std::nullopt;
    const bool toggleOff = kind.type != LineKind::Type::paragraph
                           && std::all_of(editable.begin(), editable.end(), [&](const TextLine &line) {
                                  return sameKind(MarkdownLinePrefix(line.text).kind, kind);
                              });
    const LineKind target = toggleOff ? LineKind::paragraph() : kind;

    struct Mapping {
        TextLine old;
        int newStart, oldPrefix, newPrefix;
    };
    QStringList newTexts;
    QList<Mapping> mapping;
    int location = first.start();
    int number = 1;
    for (const TextLine &line : lines) {
        const MarkdownLinePrefix parsed(line.text);
        QString newText = line.text;
        int oldPrefix = 0, newPrefix = 0;
        if (editable.contains(line)) {
            const bool keepsIndent = isList(target) && isList(parsed.kind);
            const QString indent = keepsIndent ? parsed.indent : QString();
            const QString prefix = indent + markerFor(target, number);
            if (target.type == LineKind::Type::ordered)
                ++number;
            oldPrefix = parsed.prefixLength;
            newPrefix = int(prefix.size());
            newText = prefix + line.text.mid(parsed.prefixLength);
        }
        mapping.append({line, location, oldPrefix, newPrefix});
        newTexts.append(newText);
        location += int(newText.size()) + 1;
    }
    const QString replacement = newTexts.join(QLatin1Char('\n'));
    const TextRange range(first.start(), last.end() - first.start());
    if (replacement == text.mid(range.location, range.length))
        return std::nullopt;

    auto map = [&](int position) {
        for (int i = int(mapping.size()) - 1; i >= 0; --i) {
            const Mapping &entry = mapping[i];
            if (entry.old.start() <= position) {
                const int offset = position - entry.old.start();
                return entry.newStart + entry.newPrefix + std::max(0, offset - entry.oldPrefix);
            }
        }
        return position;
    };
    TextRange newSelection;
    if (selection.length == 0) {
        newSelection = TextRange(map(selection.location), 0);
    } else {
        // A selection from the start of a line keeps the new marker in it.
        const int start = selection.location == first.start() ? first.start() : map(selection.location);
        newSelection = TextRange(start, std::max(0, map(end) - start));
    }
    return TextChange{range, replacement, newSelection};
}

} // namespace MarkdownFormatting

} // namespace wp
