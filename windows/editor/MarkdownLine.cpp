#include "editor/MarkdownLine.h"

#include <algorithm>

namespace wp {

namespace {

bool isTerminator(QChar c)
{
    const char16_t u = c.unicode();
    return u == 0x0A || u == 0x0D || u == 0x2028 || u == 0x2029 || u == 0x85;
}

bool isASCIIDigit(QChar c)
{
    return c.unicode() >= '0' && c.unicode() <= '9';
}

} // namespace

bool isAllWhitespace(const QString &text)
{
    return std::all_of(text.begin(), text.end(), [](QChar c) { return c.isSpace(); });
}

MarkdownLinePrefix::MarkdownLinePrefix(const QString &line)
{
    const int n = int(line.size());
    int i = 0;
    while (i < n && (line[i] == QLatin1Char(' ') || line[i] == QLatin1Char('\t')))
        ++i;
    indent = line.left(i);
    kind = LineKind::paragraph();
    prefixLength = i;

    auto space = [&](int j) { return j < n && line[j] == QLatin1Char(' '); };

    if (i < n && line[i] == QLatin1Char('#')) {
        int j = i;
        while (j < n && line[j] == QLatin1Char('#'))
            ++j;
        const int level = j - i;
        if (level <= 6 && space(j)) {
            kind = LineKind::heading(level);
            prefixLength = j + 1;
        }
    } else if (i < n && (line[i] == QLatin1Char('-') || line[i] == QLatin1Char('*') || line[i] == QLatin1Char('+')) && space(i + 1)) {
        const QChar bullet = line[i];
        if (i + 4 < n && line[i + 2] == QLatin1Char('[') && line[i + 4] == QLatin1Char(']')
            && (line[i + 3] == QLatin1Char(' ') || line[i + 3] == QLatin1Char('x') || line[i + 3] == QLatin1Char('X'))
            && (i + 5 == n || space(i + 5))) {
            kind = LineKind::checklist(line[i + 3] != QLatin1Char(' '), bullet);
            prefixLength = std::min(i + 6, n);
        } else {
            kind = LineKind::bullet(bullet);
            prefixLength = i + 2;
        }
    } else if (i < n && isASCIIDigit(line[i])) {
        int j = i;
        while (j < n && j - i < 9 && isASCIIDigit(line[j]))
            ++j;
        if (j < n && (line[j] == QLatin1Char('.') || line[j] == QLatin1Char(')')) && space(j + 1)) {
            kind = LineKind::ordered(line.mid(i, j - i).toInt(), line[j]);
            prefixLength = j + 2;
        }
    } else if (i < n && line[i] == QLatin1Char('>')) {
        kind = LineKind::quote();
        prefixLength = space(i + 1) ? i + 2 : i + 1;
    }
}

bool MarkdownLinePrefix::isListItem() const
{
    return kind.type == LineKind::Type::bullet || kind.type == LineKind::Type::ordered || kind.type == LineKind::Type::checklist;
}

std::optional<QString> MarkdownLinePrefix::continuationMarker() const
{
    switch (kind.type) {
    case LineKind::Type::bullet: return QString(kind.mark) + QLatin1Char(' ');
    case LineKind::Type::ordered: return QString::number(kind.number + 1) + kind.mark + QLatin1Char(' ');
    case LineKind::Type::checklist: return QString(kind.mark) + QStringLiteral(" [ ] ");
    default: return std::nullopt;
    }
}

std::optional<TextRange> MarkdownLinePrefix::checkboxRange() const
{
    if (kind.type != LineKind::Type::checklist)
        return std::nullopt;
    return TextRange(int(indent.size()) + 2, 3);
}

TextLine TextLine::at(int location, const QString &text)
{
    const int length = int(text.size());
    const int loc = std::min(std::max(location, 0), length);
    int start = loc;
    while (start > 0 && !isTerminator(text[start - 1]))
        --start;
    int contentsEnd = loc;
    while (contentsEnd < length && !isTerminator(text[contentsEnd]))
        ++contentsEnd;
    return TextLine{TextRange(start, contentsEnd - start), text.mid(start, contentsEnd - start)};
}

QList<TextLine> TextLine::all(const QString &text)
{
    QList<TextLine> lines;
    int location = 0;
    const int length = int(text.size());
    while (true) {
        const TextLine line = at(location, text);
        lines.append(line);
        if (line.end() >= length)
            break;
        location = line.end() + 1;
        // A CRLF is one terminator.
        if (text[line.end()] == QLatin1Char('\r') && location < length && text[location] == QLatin1Char('\n'))
            ++location;
    }
    if (length > 0 && isTerminator(text[length - 1]) && lines.last().end() != length)
        lines.append(TextLine{TextRange(length, 0), QString()});
    return lines;
}

namespace CodeFence {

std::optional<Opener> opener(const QString &line)
{
    int spaces = 0;
    while (spaces < line.size() && line[spaces] == QLatin1Char(' '))
        ++spaces;
    if (spaces > 3 || spaces >= line.size())
        return std::nullopt;
    const QChar marker = line[spaces];
    if (marker != QLatin1Char('`') && marker != QLatin1Char('~'))
        return std::nullopt;
    int length = 0;
    while (spaces + length < line.size() && line[spaces + length] == marker)
        ++length;
    if (length < 3)
        return std::nullopt;
    if (marker == QLatin1Char('`') && line.indexOf(QLatin1Char('`'), spaces + length) >= 0)
        return std::nullopt;
    return Opener{marker, length};
}

bool closes(const Opener &opener, const QString &line)
{
    int spaces = 0;
    while (spaces < line.size() && line[spaces] == QLatin1Char(' '))
        ++spaces;
    if (spaces > 3)
        return false;
    int run = 0;
    while (spaces + run < line.size() && line[spaces + run] == opener.marker)
        ++run;
    if (run < opener.length)
        return false;
    for (int i = spaces + run; i < line.size(); ++i) {
        if (line[i] != QLatin1Char(' ') && line[i] != QLatin1Char('\t'))
            return false;
    }
    return true;
}

std::optional<Opener> open(int lineStart, const QString &text)
{
    std::optional<Opener> open;
    for (const TextLine &line : TextLine::all(text)) {
        if (line.start() >= lineStart)
            continue;
        if (open) {
            if (closes(*open, line.text))
                open = std::nullopt;
        } else {
            open = opener(line.text);
        }
    }
    return open;
}

bool isCode(int lineStart, const QString &text)
{
    return open(lineStart, text).has_value();
}

bool isUnclosed(int lineStart, const QString &text)
{
    const TextLine line = TextLine::at(lineStart, text);
    const auto fence = opener(line.text);
    if (!fence)
        return false;
    for (const TextLine &other : TextLine::all(text)) {
        if (other.start() > line.start() && closes(*fence, other.text))
            return false;
    }
    return true;
}

} // namespace CodeFence

TextRange paragraphRange(const QString &text, TextRange range)
{
    const int length = int(text.size());
    const int location = std::min(std::max(range.location, 0), length);
    int start = location;
    while (start > 0 && !isTerminator(text[start - 1]))
        --start;
    // The paragraph holding the range's last character (or its caret).
    const int probe = range.length > 0 ? std::min(std::max(range.end() - 1, location), length) : location;
    int end = probe;
    while (end < length && !isTerminator(text[end]))
        ++end;
    if (end < length) {
        ++end;
        if (text[end - 1] == QLatin1Char('\r') && end < length && text[end] == QLatin1Char('\n'))
            ++end;
    }
    return TextRange(start, end - start);
}

} // namespace wp
