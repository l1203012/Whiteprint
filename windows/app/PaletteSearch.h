#pragma once
// Port of PaletteSearch.swift.
#include <QString>
#include <QList>
#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

namespace wp {

/// Ranks command palette entries against what's typed: a prefix beats a word start, which beats a
/// substring, which beats a scattered subsequence.
namespace PaletteSearch {

/// Higher is better; nullopt when `query` doesn't match `title` at all. An empty query scores 0.
std::optional<int> score(const QString &title, const QString &query);

/// The entries matching `query`, best first; ties keep their order. `title` returns an entry's text.
template <class T, class TitleFn>
QList<T> filter(const QList<T> &entries, const QString &query, TitleFn &&title)
{
    struct Scored {
        T entry;
        int score;
        int offset;
    };
    std::vector<Scored> scored;
    for (int i = 0; i < entries.size(); ++i)
        if (const auto s = score(QString(title(entries[i])), query))
            scored.push_back({entries[i], *s, i});
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        return a.score != b.score ? a.score > b.score : a.offset < b.offset;
    });
    QList<T> result;
    for (const Scored &s : scored)
        result.append(s.entry);
    return result;
}

} // namespace PaletteSearch

} // namespace wp
