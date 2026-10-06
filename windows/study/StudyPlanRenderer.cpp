#include "study/StudyPlanRenderer.h"

#include "core/TextUtil.h"
#include "study/StudyStore.h"

#include <QDate>
#include <QLocale>
#include <algorithm>
#include <tuple>

namespace wp::StudyPlanRenderer {

namespace {

const QString legend = QStringLiteral("★ must know · ○ good to know · ✕ can skip");

QString duration(int minutes)
{
    const int h = minutes / 60, m = minutes % 60;
    if (h == 0)
        return QStringLiteral("%1 min").arg(m);
    return m == 0 ? QStringLiteral("%1 h").arg(h) : QStringLiteral("%1 h %2 min").arg(h).arg(m);
}

/// Keeps free text from turning into note structure: a `+++page` line or a code fence (which could
/// open a drawing or swallow the rest of the page).
QString escaped(const QString &text)
{
    QStringList lines = text.split('\n');
    for (QString &line : lines) {
        qsizetype skip = 0;
        while (skip < line.size() && line[skip] == ' ')
            ++skip;
        const QString trimmed = line.mid(skip);
        if (trimmed.startsWith(QStringLiteral("+++")) || trimmed.startsWith(QStringLiteral("```"))
            || trimmed.startsWith(QStringLiteral("~~~")))
            line = QStringLiteral("\\") + trimmed;
    }
    return lines.join('\n');
}

QString refSuffix(const std::optional<QString> &ref, bool parenthesised)
{
    const QString r = oneLine(ref);
    if (r.isEmpty())
        return {};
    return parenthesised ? QStringLiteral(" *(%1)*").arg(r) : QStringLiteral(" — *%1*").arg(r);
}

QString item(const StudyPoint &point)
{
    const QString text = oneLine(point.text);
    const QString body = point.importance == Importance::must ? QStringLiteral("★ **%1**").arg(text) : QStringLiteral("○ %1").arg(text);
    return QStringLiteral("- ") + body + refSuffix(point.ref, false);
}

QString overview(const StudyPlan &plan, const QString &title)
{
    QString header = QStringLiteral("## Overview");
    int total = 0;
    for (const StudyModule &m : plan.modules) {
        if (m.minutes && *m.minutes > 0)
            total += *m.minutes;
    }
    if (total > 0)
        header += QStringLiteral(" · ≈ %1 in total").arg(duration(total));
    QStringList lines{QStringLiteral("# ") + title, QString(), header};
    const QString text = escaped(plan.overview.trimmed());
    if (!text.isEmpty())
        lines << QString() << text;
    lines << QString() << legend;
    return lines.join('\n');
}

QString learningPath(const QList<StudyModule> &modules)
{
    QStringList sections{QStringLiteral("## Learning path")};
    for (int i = 0; i < modules.size(); ++i) {
        const StudyModule &module = modules[i];
        QString heading = QStringLiteral("### %1. %2").arg(i + 1).arg(oneLine(module.title));
        if (module.minutes && *module.minutes > 0)
            heading += QStringLiteral(" · ≈ %1").arg(duration(*module.minutes));
        QList<StudyPoint> points;
        for (const StudyPoint &p : module.points) {
            if (!oneLine(p.text).isEmpty())
                points.append(p);
        }
        QList<StudyPoint> listed;
        for (const StudyPoint &p : points)
            if (p.importance == Importance::must) listed.append(p);
        for (const StudyPoint &p : points)
            if (p.importance == Importance::good) listed.append(p);
        QStringList section{heading};
        if (!listed.isEmpty()) {
            QStringList items;
            for (const StudyPoint &p : listed)
                items.append(item(p));
            section.append(items.join('\n'));
        }
        QStringList skipped;
        for (const StudyPoint &p : points) {
            if (p.importance == Importance::skip)
                skipped.append(oneLine(p.text) + refSuffix(p.ref, true));
        }
        if (!skipped.isEmpty())
            section.append(QStringLiteral("✕ Can skip: ") + skipped.join(QStringLiteral("; ")));
        sections.append(section.join(QStringLiteral("\n\n")));
    }
    return sections.join(QStringLiteral("\n\n"));
}

std::optional<QDate> date(const QString &text)
{
    static const QStringList formats = {
        "yyyy-MM-dd", "dd.MM.yyyy", "d.M.yyyy", "dd/MM/yyyy", "d MMMM yyyy", "d MMM yyyy", "MMMM d, yyyy", "MMM d, yyyy",
    };
    const QLocale c = QLocale::c();
    for (const QString &format : formats) {
        const QDate d = c.toDate(text, format);
        if (d.isValid())
            return d;
    }
    // A date at the start, e.g. `2026-01-15 23:59` or `2026-01-15 (noon)`.
    const QStringList words = text.split(' ', Qt::SkipEmptyParts);
    if (!words.isEmpty() && words[0].size() < text.size())
        return date(words[0]);
    return std::nullopt;
}

/// `week 6`, `Week 6`, `wk 6`.
std::optional<int> week(const QString &text)
{
    const QStringList words = text.toLower().split(' ', Qt::SkipEmptyParts);
    if (words.size() < 2 || (words[0] != "week" && words[0] != "wk"))
        return std::nullopt;
    QString number = words[1];
    while (!number.isEmpty() && number.front().isPunct())
        number.remove(0, 1);
    while (!number.isEmpty() && number.back().isPunct())
        number.chop(1);
    const auto n = swiftInt(number);
    if (!n)
        return std::nullopt;
    return int(*n);
}

/// Tasks with a calendar date first (earliest first), then `week N` ones, then the rest in their
/// original order.
QList<StudyTask> sortedByDue(const QList<StudyTask> &tasks)
{
    struct Keyed {
        int group;
        double value;
        int index;
    };
    std::vector<Keyed> keyed;
    for (int i = 0; i < tasks.size(); ++i) {
        const QString due = oneLine(tasks[i].due);
        if (const auto d = date(due))
            keyed.push_back({0, double(d->toJulianDay()), i});
        else if (const auto w = week(due))
            keyed.push_back({1, double(*w), i});
        else
            keyed.push_back({2, 0, i});
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const Keyed &a, const Keyed &b) {
        return std::tie(a.group, a.value) < std::tie(b.group, b.value);
    });
    QList<StudyTask> result;
    for (const Keyed &k : keyed)
        result.append(tasks[k.index]);
    return result;
}

QString toDo(const QList<StudyTask> &tasks)
{
    QStringList lines{QStringLiteral("## To-do"), QString()};
    for (const StudyTask &task : sortedByDue(tasks)) {
        QString line = QStringLiteral("- [ ] ") + oneLine(task.text);
        const QString due = oneLine(task.due);
        if (!due.isEmpty())
            line += QStringLiteral(" — due ") + due;
        lines.append(line + refSuffix(task.ref, false));
    }
    return lines.join('\n');
}

} // namespace

Note note(const StudyPlan &plan)
{
    const QString title = oneLine(plan.title).isEmpty() ? QStringLiteral("Study plan") : oneLine(plan.title);
    // One text block per page: adjacent text blocks would merge when the note is read back.
    QList<NotePage> pages{NotePage({NoteBlock::textBlock(overview(plan, title) + QStringLiteral("\n\n") + learningPath(plan.modules))})};
    if (!plan.tasks.isEmpty())
        pages.append(NotePage({NoteBlock::textBlock(toDo(plan.tasks))}));
    QList<Flashcard> cards;
    for (const Flashcard &c : plan.flashcards) {
        if (!oneLine(c.question).isEmpty() || !oneLine(c.answer).isEmpty())
            cards.append(c);
    }
    if (!cards.isEmpty()) {
        pages.append(NotePage({NoteBlock::textBlock(QStringLiteral("## Flashcards")),
                               NoteBlock::cardsBlock(CardDeck(QString(), title, cards))}));
    }
    QList<NoteBlock> diagramBlocks{NoteBlock::textBlock(QStringLiteral("## Diagrams"))};
    for (const QString &d : plan.diagrams) {
        const QString source = d.trimmed();
        if (!source.isEmpty())
            diagramBlocks.append(NoteBlock::drawingBlock(Drawing(QString(), source)));
    }
    if (diagramBlocks.size() > 1)
        pages.append(NotePage(diagramBlocks));
    return Note(title, pages);
}

} // namespace wp::StudyPlanRenderer
