#include "core/TextUtil.h"

#include <QLocale>
#include <QTextBoundaryFinder>
#include <cmath>

namespace wp {

QString trimmedWhitespace(QStringView text)
{
    auto isWs = [](QChar c) { return c == QLatin1Char('\t') || c.category() == QChar::Separator_Space; };
    qsizetype begin = 0, end = text.size();
    while (begin < end && isWs(text[begin]))
        ++begin;
    while (end > begin && isWs(text[end - 1]))
        --end;
    return text.mid(begin, end - begin).toString();
}

bool isNewlineChar(QChar c)
{
    const char16_t u = c.unicode();
    return u == 0x0A || u == 0x0B || u == 0x0C || u == 0x0D || u == 0x85 || u == 0x2028 || u == 0x2029;
}

std::optional<qint64> swiftInt(QStringView text)
{
    qsizetype i = 0;
    if (i < text.size() && (text[i] == QLatin1Char('+') || text[i] == QLatin1Char('-')))
        ++i;
    if (i >= text.size())
        return std::nullopt;
    for (qsizetype j = i; j < text.size(); ++j) {
        if (text[j] < QLatin1Char('0') || text[j] > QLatin1Char('9'))
            return std::nullopt;
    }
    bool ok = false;
    const qint64 value = text.toLongLong(&ok);
    if (!ok)
        return std::nullopt;
    return value;
}

int graphemeCount(QStringView text)
{
    if (text.isEmpty())
        return 0;
    const QString s = text.toString();
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, s);
    int count = 0;
    while (finder.toNextBoundary() != -1)
        ++count;
    return count;
}

QString formatDouble(double value)
{
    if (value == std::round(value) && std::abs(value) < 1e15)
        return QString::number(static_cast<qint64>(value));
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

bool isLetterCodePoint(char32_t c)
{
    return QChar::isLetter(c);
}

bool isNumberCodePoint(char32_t c)
{
    return QChar::isNumber(c);
}

char32_t codePointAt(QStringView text, qsizetype index, int *length)
{
    const QChar c = text[index];
    if (c.isHighSurrogate() && index + 1 < text.size() && text[index + 1].isLowSurrogate()) {
        if (length)
            *length = 2;
        return QChar::surrogateToUcs4(c, text[index + 1]);
    }
    if (length)
        *length = 1;
    return c.unicode();
}

} // namespace wp
