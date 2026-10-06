#include "extract/TextCleaner.h"

#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <set>

namespace wp::TextCleaner {

namespace {

bool isNewline(QChar c) {
    ushort u = c.unicode();
    return u == '\n' || u == '\r' || u == 0x0B || u == 0x0C || u == 0x85 || u == 0x2028 || u == 0x2029;
}

bool endsWithBrokenWord(const QString &line) {
    if (line.size() < 2 || !line.endsWith(QLatin1Char('-'))) return false;
    return line[line.size() - 2].isLetter();
}

/// Lines this close to the start or end of a unit count as header or footer.
constexpr int edgeDepth = 2;
/// Running headers are short; longer lines are only removed when repeated exactly.
constexpr int maxMaskedLength = 80;

struct EdgeKey {
    int index;
    QStringList keys;
};

std::vector<EdgeKey> edgeKeys(const QStringList &lines) {
    std::vector<int> content;
    for (int i = 0; i < lines.size(); ++i)
        if (!lines[i].isEmpty()) content.push_back(i);
    std::vector<EdgeKey> out;
    if (content.empty()) return out;
    std::set<int> outer{content.front(), content.back()};
    std::set<int> selected;
    for (int i = 0; i < int(content.size()); ++i)
        if (i < edgeDepth || i >= int(content.size()) - edgeDepth) selected.insert(content[i]);
    for (int index : selected) {
        QString exact = lines[index].toLower();
        if (!outer.count(index) || exact.size() > maxMaskedLength) {
            out.push_back({index, {exact}});
            continue;
        }
        QString masked = QStringLiteral("#");
        for (QChar c : exact) masked += c.isNumber() ? QLatin1Char('#') : c;
        out.push_back({index, {exact, masked}});
    }
    return out;
}

} // namespace

std::vector<ExtractedUnit> clean(const std::vector<ExtractedUnit> &units) {
    std::vector<QStringList> lines;
    lines.reserve(units.size());
    for (const auto &u : units) lines.push_back(cleanLines(u.text));
    removeRepeatedEdges(lines);
    std::vector<ExtractedUnit> out;
    for (size_t i = 0; i < units.size(); ++i) {
        QString text = joinParagraphs(lines[i]);
        if (!text.isEmpty()) out.push_back({units[i].ref, text});
    }
    return out;
}

QStringList cleanLines(const QString &text) {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.remove(QChar(0x00AD));
    QStringList lines;
    qsizetype start = 0;
    auto handle = [&](const QString &rawLine) {
        QString line = collapseWhitespace(rawLine);
        if (isPageNumber(line)) return;
        if (!lines.isEmpty() && endsWithBrokenWord(lines.last()) && !line.isEmpty() && line[0].isLower()) {
            QString last = lines.last();
            last.chop(1);
            lines.last() = last + line;
        } else {
            lines << line;
        }
    };
    for (qsizetype i = 0; i < normalized.size(); ++i) {
        if (isNewline(normalized[i])) {
            handle(normalized.mid(start, i - start));
            start = i + 1;
        }
    }
    handle(normalized.mid(start));
    return lines;
}

QString joinParagraphs(const QStringList &lines) {
    QStringList result;
    for (const QString &line : lines) {
        if (line.isEmpty() && (result.isEmpty() || result.last().isEmpty())) continue;
        result << line;
    }
    while (!result.isEmpty() && result.last().isEmpty()) result.removeLast();
    return result.join(QLatin1Char('\n'));
}

QString collapseWhitespace(const QString &text) {
    QString out;
    out.reserve(text.size());
    bool pending = false;
    for (QChar c : text) {
        if (c.isSpace()) {
            pending = !out.isEmpty();
        } else {
            if (pending) out += QLatin1Char(' ');
            pending = false;
            out += c;
        }
    }
    return out;
}

bool isPageNumber(const QString &line) {
    if (line.isEmpty() || line.size() > 24) return false;
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[-\x{2013}\x{2014}|\s]*(?:(?:page|pagina|blz\.?|p\.|pg\.?|seite|s\.)\s*)?\d{1,4})"
                       R"((?:\s*(?:/|of|van|de|sur|von)\s*\d{1,4})?[-\x{2013}\x{2014}|\s]*$)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return pattern.match(line).hasMatch();
}

void removeRepeatedEdges(std::vector<QStringList> &units) {
    if (units.size() < 3) return;
    QHash<QString, int> counts;
    for (const auto &lines : units) {
        QSet<QString> keys;
        for (const auto &e : edgeKeys(lines))
            for (const auto &k : e.keys) keys.insert(k);
        for (const auto &k : keys) counts[k] += 1;
    }
    QSet<QString> repeated;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() * 2 > int(units.size())) repeated.insert(it.key());
    if (repeated.isEmpty()) return;

    for (auto &lines : units) {
        auto edges = edgeKeys(lines);
        std::set<int> drop;
        for (const auto &e : edges)
            for (const auto &k : e.keys)
                if (repeated.contains(k)) { drop.insert(e.index); break; }
        // Digit-insensitive matches never empty a unit: `Slide 3` alone is content.
        int nonEmpty = int(std::count_if(lines.begin(), lines.end(), [](const QString &l) { return !l.isEmpty(); }));
        if (int(drop.size()) == nonEmpty) {
            drop.clear();
            for (const auto &e : edges)
                if (repeated.contains(e.keys[0])) drop.insert(e.index);
        }
        QStringList kept;
        for (int i = 0; i < lines.size(); ++i)
            if (!drop.count(i)) kept << lines[i];
        lines = kept;
    }
}

} // namespace wp::TextCleaner
