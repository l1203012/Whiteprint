#include "core/DSLTokenizer.h"

#include "core/TextUtil.h"

#include <cmath>

namespace wp {
namespace DSLTokenizer {
namespace {

bool isLinkCharacter(QChar c)
{
    return c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('-');
}

bool isAsciiDigitOrDot(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('.');
}

/// Reads a quoted string starting at `start` (the opening quote). Supports `\"`, `\\` and `\n`.
QString readString(QStringView line, qsizetype start, qsizetype *end)
{
    QString result;
    qsizetype i = start + 1;
    while (i < line.size()) {
        const QChar c = line[i];
        ++i;
        if (c == QLatin1Char('"')) {
            *end = i;
            return result;
        }
        if (c == QLatin1Char('\\') && i < line.size()) {
            const QChar escaped = line[i];
            ++i;
            result.append(escaped == QLatin1Char('n') ? QChar(u'\n') : escaped);
        } else {
            result.append(c);
        }
    }
    throw DSLSyntaxError{QStringLiteral("unterminated \"label\"")};
}

/// Plain decimal numbers only, so `inf`, `nan` and `1e5` are not numbers.
std::optional<double> number(QStringView raw)
{
    const QStringView digits = raw.startsWith(QLatin1Char('-')) ? raw.mid(1) : raw;
    if (digits.isEmpty())
        return std::nullopt;
    int dots = 0;
    for (QChar c : digits) {
        if (!isAsciiDigitOrDot(c))
            return std::nullopt;
        if (c == QLatin1Char('.'))
            ++dots;
    }
    if (dots > 1 || digits == QLatin1String("."))
        return std::nullopt;
    bool ok = false;
    const double value = raw.toDouble(&ok);
    if (!ok)
        return std::nullopt;
    if (std::abs(value) > maxMagnitude) {
        throw DSLSyntaxError{QStringLiteral("'%1' is too large, max %2").arg(raw.toString()).arg(int(maxMagnitude))};
    }
    return value;
}

/// Parses `a>b`, `a->b`, `a<>b`, `a<-b`, `a-b`, chained any number of times.
DSLChain chain(QStringView raw)
{
    const DSLSyntaxError invalid{QStringLiteral("bad link '%1', use a>b").arg(raw.toString())};
    DSLChain result;
    QStringView rest = raw;
    while (true) {
        qsizetype idLength = 0;
        while (idLength < rest.size() && !isLinkCharacter(rest[idLength]))
            ++idLength;
        const QStringView id = rest.left(idLength);
        if (!isIdentifier(id))
            throw invalid;
        result.ids.append(id.toString());
        rest = rest.mid(idLength);
        if (rest.isEmpty())
            break;

        qsizetype opLength = 0;
        while (opLength < rest.size() && isLinkCharacter(rest[opLength]))
            ++opLength;
        const QStringView op = rest.left(opLength);
        rest = rest.mid(opLength);
        const bool startArrow = op.startsWith(QLatin1Char('<'));
        const bool endArrow = op.endsWith(QLatin1Char('>'));
        QStringView middle = op;
        if (startArrow)
            middle = middle.mid(1);
        if (endArrow)
            middle = middle.left(middle.size() - 1);
        for (QChar c : middle) {
            if (c != QLatin1Char('-'))
                throw invalid;
        }
        result.links.append(DSLLink(startArrow, endArrow));
    }
    if (result.ids.size() < 2)
        throw invalid;
    return result;
}

DSLToken classify(const QString &raw)
{
    if (auto n = number(raw))
        return DSLToken::number(*n);
    if (raw.contains(QLatin1Char(','))) {
        const QList<QStringView> parts = QStringView(raw).split(QLatin1Char(','), Qt::KeepEmptyParts);
        if (parts.size() == 2) {
            if (auto x = number(parts[0])) {
                if (auto y = number(parts[1]))
                    return DSLToken::point(GridPoint(*x, *y));
            }
        }
        throw DSLSyntaxError{QStringLiteral("bad position '%1', use X,Y").arg(raw)};
    }
    const char32_t first = codePointAt(raw, 0);
    if (isNumberCodePoint(first) || first == U'.') {
        const QList<QStringView> parts = QStringView(raw).split(QLatin1Char('x'), Qt::KeepEmptyParts);
        if (parts.size() == 2) {
            if (auto width = number(parts[0])) {
                if (auto height = number(parts[1])) {
                    if (!(*width > 0) || !(*height > 0))
                        throw DSLSyntaxError{QStringLiteral("size '%1' must be positive").arg(raw)};
                    return DSLToken::size(GridSize(*width, *height));
                }
            }
        }
        throw DSLSyntaxError{QStringLiteral("bad size '%1', use WxH").arg(raw)};
    }
    for (QChar c : raw) {
        if (isLinkCharacter(c))
            return DSLToken::chain(chain(raw));
    }
    if (!isIdentifier(raw))
        throw DSLSyntaxError{QStringLiteral("unexpected '%1'").arg(raw)};
    return DSLToken::word(raw);
}

} // namespace

QList<DSLToken> tokenize(QStringView line)
{
    QList<DSLToken> tokens;
    qsizetype i = 0;
    while (i < line.size()) {
        const QChar c = line[i];
        if (c.isSpace()) {
            ++i;
        } else if (c == QLatin1Char('#')) {
            break;
        } else if (c == QLatin1Char('"')) {
            qsizetype end = 0;
            QString s = readString(line, i, &end);
            tokens.append(DSLToken::string(std::move(s)));
            i = end;
        } else {
            qsizetype end = i;
            while (end < line.size() && !line[end].isSpace() && line[end] != QLatin1Char('#')
                   && line[end] != QLatin1Char('"'))
                ++end;
            tokens.append(classify(line.mid(i, end - i).toString()));
            i = end;
        }
    }
    return tokens;
}

bool isIdentifier(QStringView raw)
{
    if (raw.isEmpty())
        return false;
    qsizetype i = 0;
    bool first = true;
    while (i < raw.size()) {
        int length = 1;
        const char32_t c = codePointAt(raw, i, &length);
        i += length;
        if (first) {
            if (!(isLetterCodePoint(c) || c == U'_'))
                return false;
            first = false;
        } else if (!(isLetterCodePoint(c) || isNumberCodePoint(c) || c == U'_' || QChar::isMark(c))) {
            return false;
        }
    }
    return true;
}

} // namespace DSLTokenizer
} // namespace wp
