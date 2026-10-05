#include "app/PaletteSearch.h"

#include "app/AppUtil.h"
#include "core/TextUtil.h"

namespace wp::PaletteSearch {

std::optional<int> score(const QString &rawTitle, const QString &rawQuery)
{
    const QString query = trimmedWhitespace(rawQuery).toLower();
    if (query.isEmpty())
        return 0;
    const QString title = rawTitle.toLower();
    const int length = graphemeCount(title);
    if (title.startsWith(query))
        return 400 - length;
    const qsizetype index = title.indexOf(query);
    if (index >= 0) {
        const bool atWordStart = index == 0 || !title.at(index - 1).isLetter();
        return (atWordStart ? 300 : 200) - length;
    }
    qsizetype next = 0;
    for (const QChar c : title) {
        if (c == query.at(next)) {
            ++next;
            if (next == query.size())
                return 100 - length;
        }
    }
    return std::nullopt;
}

} // namespace wp::PaletteSearch
