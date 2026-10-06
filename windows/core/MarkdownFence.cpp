#include "core/MarkdownFence.h"

#include "core/TextUtil.h"

namespace wp {
namespace {

/// Removes up to three leading spaces; four or more means an indented code line.
std::optional<QStringView> stripIndent(QStringView line)
{
    qsizetype spaces = 0;
    while (spaces < line.size() && line[spaces] == QLatin1Char(' '))
        ++spaces;
    if (spaces > 3)
        return std::nullopt;
    return line.mid(spaces);
}

qsizetype runLength(QStringView text, QChar c)
{
    qsizetype n = 0;
    while (n < text.size() && text[n] == c)
        ++n;
    return n;
}

} // namespace

std::optional<MarkdownFence> MarkdownFence::opening(QStringView line)
{
    const auto rest = stripIndent(line);
    if (!rest || rest->isEmpty())
        return std::nullopt;
    const QChar marker = rest->at(0);
    if (marker != QLatin1Char('`') && marker != QLatin1Char('~'))
        return std::nullopt;
    const qsizetype length = runLength(*rest, marker);
    if (length < 3)
        return std::nullopt;
    const QString info = trimmedWhitespace(rest->mid(length));
    if (marker == QLatin1Char('`') && info.contains(QLatin1Char('`')))
        return std::nullopt;
    MarkdownFence fence;
    fence.marker = marker;
    fence.length = int(length);
    fence.info = info;
    return fence;
}

bool MarkdownFence::isClosed(QStringView line) const
{
    const auto rest = stripIndent(line);
    if (!rest)
        return false;
    const qsizetype run = runLength(*rest, marker);
    if (run < length)
        return false;
    for (QChar c : rest->mid(run)) {
        if (c != QLatin1Char(' ') && c != QLatin1Char('\t'))
            return false;
    }
    return true;
}

QStringList MarkdownFence::infoWords() const
{
    QStringList words;
    QString current;
    for (QChar c : info) {
        if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
            if (!current.isEmpty())
                words.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    if (!current.isEmpty())
        words.append(current);
    return words;
}

std::optional<QString> MarkdownFence::attribute(const QString &key) const
{
    const QString prefix = key + QLatin1Char('=');
    const QStringList words = infoWords();
    for (qsizetype i = 1; i < words.size(); ++i) {
        if (words[i].startsWith(prefix))
            return words[i].mid(prefix.size());
    }
    return std::nullopt;
}

std::optional<MarkdownFence> MarkdownFence::unclosed(const QString &text)
{
    std::optional<MarkdownFence> open;
    for (const QStringView &line : QStringView(text).split(QLatin1Char('\n'), Qt::KeepEmptyParts)) {
        if (open) {
            if (open->isClosed(line))
                open.reset();
        } else {
            open = opening(line);
        }
    }
    return open;
}

} // namespace wp
