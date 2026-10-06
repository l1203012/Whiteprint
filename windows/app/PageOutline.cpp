#include "app/PageOutline.h"

#include "app/AppUtil.h"
#include "core/TextUtil.h"

namespace wp::PageOutline {

QString title(const NotePage &page, int number)
{
    for (const NoteBlock &block : page.blocks) {
        if (block.kind != BlockKind::text)
            continue;
        QStringList lines;
        for (const QString &raw : block.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString line = trimmedWhitespace(raw);
            if (!line.isEmpty())
                lines.append(line);
        }
        QString heading;
        bool found = false;
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1Char('#'))) {
                heading = line;
                found = true;
                break;
            }
        }
        if (!found && !lines.isEmpty()) {
            heading = lines.first();
            found = true;
        }
        if (found) {
            qsizetype start = 0;
            while (start < heading.size() && heading.at(start) == QLatin1Char('#'))
                ++start;
            const QString label = trimmedWhitespace(heading.mid(start));
            if (!label.isEmpty())
                return prefixGraphemes(label, 60);
        }
    }
    return QStringLiteral("Page %1").arg(number);
}

QStringList titles(const Note &note)
{
    QStringList result;
    const auto &pages = note.pages();
    for (qsizetype i = 0; i < pages.size(); ++i)
        result.append(title(pages[i], int(i) + 1));
    return result;
}

} // namespace wp::PageOutline
