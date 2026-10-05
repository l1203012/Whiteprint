#include "render/MarkdownSyntax.h"

#include <algorithm>
#include <vector>

namespace wp {

QString MarkdownFenceState::encoded() const
{
    return QString(QChar(marker)) + QString::number(length);
}

std::optional<MarkdownFenceState> MarkdownFenceState::fromEncoded(const QString &encoded)
{
    if (encoded.isEmpty())
        return std::nullopt;
    bool ok = false;
    const int length = encoded.mid(1).toInt(&ok);
    if (!ok)
        return std::nullopt;
    return MarkdownFenceState(encoded.at(0).unicode(), length);
}

QList<TextRange> MarkdownSpan::markupRanges() const
{
    QList<TextRange> out;
    const TextRange before(range.location, contentRange.location - range.location);
    const TextRange after(contentRange.end(), range.end() - contentRange.end());
    if (before.length > 0)
        out.append(before);
    if (after.length > 0)
        out.append(after);
    return out;
}

namespace MarkdownSyntax {

namespace {

using Chars = std::vector<char16_t>;

int runOf(char16_t marker, const Chars &c, int index)
{
    int n = 0;
    while (index + n < int(c.size()) && c[index + n] == marker)
        ++n;
    return n;
}

int spacesAt(const Chars &c, int index)
{
    int n = 0;
    while (index + n < int(c.size()) && isWhitespace(c[index + n]))
        ++n;
    return n;
}

bool restIsWhitespace(const Chars &c, int from)
{
    for (int i = from; i < int(c.size()); ++i)
        if (!isWhitespace(c[i]))
            return false;
    return true;
}

bool restContains(const Chars &c, int from, char16_t ch)
{
    for (int i = from; i < int(c.size()); ++i)
        if (c[i] == ch)
            return true;
    return false;
}

MarkdownLine classifyBlock(const Chars &c, int indent)
{
    const int count = int(c.size());
    const char16_t first = c[indent];
    if (indent <= 3 && first == hash) {
        const int level = runOf(hash, c, indent);
        const int end = indent + level;
        if (level <= 6 && (end == count || isWhitespace(c[end])))
            return MarkdownLine::heading(level, end + spacesAt(c, end));
    }
    if (indent <= 3 && (first == dash || first == star || first == underscore)) {
        int same = 0;
        bool only = true;
        for (int i = indent; i < count; ++i) {
            if (c[i] == first)
                ++same;
            else if (!isWhitespace(c[i]))
                only = false;
        }
        if (same >= 3 && only)
            return MarkdownLine::of(MarkdownLineKind::divider, count);
    }
    if ((first == dash || first == star || first == plus) && indent + 1 < count && isWhitespace(c[indent + 1])) {
        const int afterMarker = indent + 1 + spacesAt(c, indent + 1);
        if (afterMarker + 2 < count && c[afterMarker] == openBracket && c[afterMarker + 2] == closeBracket
            && (afterMarker + 3 == count || isWhitespace(c[afterMarker + 3]))) {
            const char16_t mark = c[afterMarker + 1];
            if (mark == space || mark == lowerX || mark == upperX) {
                const int end = afterMarker + 3;
                return MarkdownLine::task(mark != space, end + spacesAt(c, end));
            }
        }
        return MarkdownLine::of(MarkdownLineKind::bullet, afterMarker);
    }
    int digits = 0;
    while (indent + digits < count && isDigit(c[indent + digits]))
        ++digits;
    if (digits >= 1 && digits <= 9 && indent + digits + 1 < count) {
        const char16_t delimiter = c[indent + digits];
        if ((delimiter == dot || delimiter == closeParen) && isWhitespace(c[indent + digits + 1])) {
            const int end = indent + digits + 1;
            return MarkdownLine::of(MarkdownLineKind::ordered, end + spacesAt(c, end));
        }
    }
    if (indent <= 3 && first == greater) {
        int length = 0;
        while (indent + length < count && (c[indent + length] == greater || isWhitespace(c[indent + length])))
            ++length;
        return MarkdownLine::of(MarkdownLineKind::quote, indent + length);
    }
    return MarkdownLine::of(MarkdownLineKind::paragraph);
}

/// Finds inline spans in one line: code spans first (nothing inside them is
/// markup), then links, then `**bold**` / `__bold__`, then `*italic*` / `_italic_`.
class InlineScanner
{
public:
    explicit InlineScanner(Chars chars) : c(std::move(chars)), taken(c.size(), false) {}

    QList<MarkdownSpan> scan()
    {
        markEscapes();
        scanCode();
        scanLinks();
        scanEmphasis(2, MarkdownSpan::Kind::bold);
        scanEmphasis(1, MarkdownSpan::Kind::italic);
        QList<MarkdownSpan> sorted = spans;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const MarkdownSpan &a, const MarkdownSpan &b) { return a.range.location < b.range.location; });
        return sorted;
    }

private:
    Chars c;
    std::vector<bool> taken;
    QList<MarkdownSpan> spans;

    int count() const { return int(c.size()); }

    void markEscapes()
    {
        int i = 0;
        while (i + 1 < count()) {
            if (c[i] == backslash && isASCIIPunctuation(c[i + 1])) {
                taken[i + 1] = true;
                i += 2;
            } else {
                i += 1;
            }
        }
    }

    void scanCode()
    {
        int i = 0;
        while (i < count()) {
            if (c[i] != backtick || taken[i]) {
                ++i;
                continue;
            }
            const int length = runOf(backtick, c, i);
            int j = i + length;
            int close = -1;
            while (j < count()) {
                if (c[j] == backtick) {
                    const int run = runOf(backtick, c, j);
                    if (run == length) {
                        close = j;
                        break;
                    }
                    j += run;
                } else {
                    ++j;
                }
            }
            if (close < 0 || close <= i + length) {
                i += length;
                continue;
            }
            add(MarkdownSpan::Kind::code, i, close + length, TextRange(i + length, close - i - length));
            i = close + length;
        }
    }

    void scanLinks()
    {
        int i = 0;
        while (i < count()) {
            if (c[i] != openBracket || taken[i]) {
                ++i;
                continue;
            }
            const int close = firstFree(closeBracket, i + 1);
            if (close < 0 || close + 1 >= count() || c[close + 1] != openParen) {
                ++i;
                continue;
            }
            const int end = firstFree(closeParen, close + 2);
            if (end < 0) {
                ++i;
                continue;
            }
            const QString url = QString::fromUtf16(c.data() + close + 2, end - close - 2);
            add(MarkdownSpan::Kind::link, i, end + 1, TextRange(i + 1, close - i - 1), url);
            taken[i] = true;
            for (int k = close; k <= end; ++k)
                taken[k] = true;
            i = end + 1;
        }
    }

    void scanEmphasis(int width, MarkdownSpan::Kind kind)
    {
        int i = 0;
        while (i + width < count()) {
            const char16_t marker = delimiter(i, width);
            if (marker == 0 || !canOpen(i, width, marker)) {
                ++i;
                continue;
            }
            const int close = closer(marker, width, i + width + 1);
            if (close < 0) {
                ++i;
                continue;
            }
            add(kind, i, close + width, TextRange(i + width, close - i - width));
            for (int k = 0; k < width; ++k) {
                taken[i + k] = true;
                taken[close + k] = true;
            }
            i = close + width;
        }
    }

    /// The `*` or `_` starting a free run of exactly `width` characters at `i`, or 0.
    char16_t delimiter(int i, int width) const
    {
        const char16_t marker = c[i];
        if ((marker != star && marker != underscore) || i + width > count())
            return 0;
        for (int k = 0; k < width; ++k)
            if (c[i + k] != marker || taken[i + k])
                return 0;
        return marker;
    }

    bool canOpen(int i, int width, char16_t marker) const
    {
        const int next = i + width;
        if (next >= count() || isWhitespace(c[next]))
            return false;
        return marker != underscore || i == 0 || !isWordCharacter(c[i - 1]);
    }

    int closer(char16_t marker, int width, int start) const
    {
        int j = start;
        while (j + width <= count()) {
            if (delimiter(j, width) == marker && !isWhitespace(c[j - 1])
                && (marker != underscore || j + width == count() || !isWordCharacter(c[j + width])))
                return j;
            ++j;
        }
        return -1;
    }

    int firstFree(char16_t character, int start) const
    {
        for (int j = start; j < count(); ++j)
            if (c[j] == character && !taken[j])
                return j;
        return -1;
    }

    void add(MarkdownSpan::Kind kind, int start, int end, TextRange content, std::optional<QString> url = std::nullopt)
    {
        MarkdownSpan span;
        span.kind = kind;
        span.range = TextRange(start, end - start);
        span.contentRange = content;
        span.url = std::move(url);
        spans.append(span);
        if (kind == MarkdownSpan::Kind::code)
            for (int k = start; k < end; ++k)
                taken[k] = true;
    }
};

} // namespace

Classified classify(const QString &line, const std::optional<MarkdownFenceState> &openFence)
{
    const Chars c(reinterpret_cast<const char16_t *>(line.utf16()), reinterpret_cast<const char16_t *>(line.utf16()) + line.size());
    const int count = int(c.size());
    int indent = 0;
    while (indent < count && (c[indent] == space || c[indent] == tab))
        ++indent;
    if (openFence) {
        const int length = indent <= 3 ? runOf(openFence->marker, c, indent) : 0;
        const bool closes = length >= openFence->length && restIsWhitespace(c, indent + length);
        if (closes)
            return {MarkdownLine::of(MarkdownLineKind::fence), std::nullopt};
        return {MarkdownLine::of(MarkdownLineKind::code), openFence};
    }
    if (indent == count)
        return {MarkdownLine::of(MarkdownLineKind::blank), std::nullopt};
    const char16_t first = c[indent];
    if (indent <= 3 && (first == backtick || first == tilde)) {
        const int length = runOf(first, c, indent);
        if (length >= 3 && !(first == backtick && restContains(c, indent + length, backtick)))
            return {MarkdownLine::of(MarkdownLineKind::fence), MarkdownFenceState(first, length)};
    }
    return {classifyBlock(c, indent), std::nullopt};
}

QList<MarkdownSpan> spans(const QString &text, int offset)
{
    const char16_t *data = reinterpret_cast<const char16_t *>(text.utf16());
    InlineScanner scanner(Chars(data, data + text.size()));
    QList<MarkdownSpan> found = scanner.scan();
    for (MarkdownSpan &span : found) {
        span.range.location += offset;
        span.contentRange.location += offset;
    }
    return found;
}

} // namespace MarkdownSyntax

} // namespace wp
