#include "editor/SlashCommand.h"

#include "core/TextUtil.h"

#include <algorithm>

namespace wp {

namespace SlashCommands {

QList<SlashCommand> all()
{
    return {SlashCommand::heading1,   SlashCommand::heading2,    SlashCommand::heading3,   SlashCommand::bulletList,
            SlashCommand::numberedList, SlashCommand::checklist, SlashCommand::quote,      SlashCommand::codeBlock,
            SlashCommand::divider,    SlashCommand::drawing,     SlashCommand::flashcards, SlashCommand::newPage};
}

QString title(SlashCommand command)
{
    switch (command) {
    case SlashCommand::heading1: return QStringLiteral("Heading 1");
    case SlashCommand::heading2: return QStringLiteral("Heading 2");
    case SlashCommand::heading3: return QStringLiteral("Heading 3");
    case SlashCommand::bulletList: return QStringLiteral("Bulleted list");
    case SlashCommand::numberedList: return QStringLiteral("Numbered list");
    case SlashCommand::checklist: return QStringLiteral("Checklist");
    case SlashCommand::quote: return QStringLiteral("Quote");
    case SlashCommand::codeBlock: return QStringLiteral("Code block");
    case SlashCommand::divider: return QStringLiteral("Divider");
    case SlashCommand::drawing: return QStringLiteral("Drawing");
    case SlashCommand::flashcards: return QStringLiteral("Flashcards");
    case SlashCommand::newPage: return QStringLiteral("New page");
    }
    return {};
}

QString subtitle(SlashCommand command)
{
    switch (command) {
    case SlashCommand::heading1: return QStringLiteral("Big section heading");
    case SlashCommand::heading2: return QStringLiteral("Medium section heading");
    case SlashCommand::heading3: return QStringLiteral("Small section heading");
    case SlashCommand::bulletList: return QStringLiteral("A simple bulleted list");
    case SlashCommand::numberedList: return QStringLiteral("A list with numbering");
    case SlashCommand::checklist: return QStringLiteral("Track tasks with checkboxes");
    case SlashCommand::quote: return QStringLiteral("Capture a quote");
    case SlashCommand::codeBlock: return QStringLiteral("Monospaced code");
    case SlashCommand::divider: return QStringLiteral("Visually divide blocks");
    case SlashCommand::drawing: return QStringLiteral("A diagram in the drawing language");
    case SlashCommand::flashcards: return QStringLiteral("A deck of question and answer cards");
    case SlashCommand::newPage: return QStringLiteral("Start a new page after this one");
    }
    return {};
}

namespace {

/// Extra words the filter matches, besides the title.
QStringList keywords(SlashCommand command)
{
    switch (command) {
    case SlashCommand::heading1: return {"h1", "title", "#"};
    case SlashCommand::heading2: return {"h2", "subtitle", "##"};
    case SlashCommand::heading3: return {"h3", "###"};
    case SlashCommand::bulletList: return {"ul", "unordered", "-"};
    case SlashCommand::numberedList: return {"ol", "ordered", "1."};
    case SlashCommand::checklist: return {"todo", "task", "checkbox", "[]"};
    case SlashCommand::quote: return {"blockquote", ">"};
    case SlashCommand::codeBlock: return {"```", "snippet", "pre"};
    case SlashCommand::divider: return {"hr", "rule", "line", "---", "separator"};
    case SlashCommand::drawing: return {"diagram", "sketch", "wp", "draw", "chart"};
    case SlashCommand::flashcards: return {"cards", "deck", "study", "quiz", "q&a"};
    case SlashCommand::newPage: return {"page", "break", "+++"};
    }
    return {};
}

std::optional<int> rank(SlashCommand command, const QString &query, const QString &squashed)
{
    const QString title = SlashCommands::title(command).toLower();
    QString titleSquashed = title;
    titleSquashed.remove(QLatin1Char(' '));
    if (title.startsWith(query) || titleSquashed.startsWith(squashed))
        return 0;
    for (const QString &word : title.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word.startsWith(query))
            return 1;
    }
    for (const QString &keyword : keywords(command)) {
        if (keyword.startsWith(query))
            return 2;
    }
    if (title.contains(query))
        return 3;
    return std::nullopt;
}

} // namespace

QList<SlashCommand> matching(const QString &rawQuery)
{
    const QString query = trimmedWhitespace(rawQuery.toLower());
    if (query.isEmpty())
        return all();
    QString squashed = query;
    squashed.remove(QLatin1Char(' '));
    struct Ranked {
        SlashCommand command;
        int rank;
        int offset;
    };
    QList<Ranked> ranked;
    int offset = 0;
    for (SlashCommand command : all()) {
        if (auto r = rank(command, query, squashed))
            ranked.append({command, *r, offset});
        ++offset;
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
        return a.rank != b.rank ? a.rank < b.rank : a.offset < b.offset;
    });
    QList<SlashCommand> result;
    for (const Ranked &r : ranked)
        result.append(r.command);
    return result;
}

} // namespace SlashCommands

std::optional<SlashCommand> SlashMenuState::selected() const
{
    if (m_selectedIndex >= 0 && m_selectedIndex < m_items.size())
        return m_items[m_selectedIndex];
    return std::nullopt;
}

bool SlashMenuState::update(const QString &text, int caret)
{
    const int length = int(text.size());
    if (m_slashLocation >= length || text[m_slashLocation] != QLatin1Char('/') || caret <= m_slashLocation)
        return false;
    const QString typed = text.mid(m_slashLocation + 1, caret - m_slashLocation - 1);
    if (typed.contains(QLatin1Char('\n')) || graphemeCount(typed) > 24)
        return false;
    const QList<SlashCommand> matches = SlashCommands::matching(typed);
    if (matches.isEmpty() && typed.endsWith(QLatin1Char(' ')))
        return false;
    if (typed != m_query) {
        m_query = typed;
        m_items = matches;
        m_selectedIndex = 0;
    }
    return true;
}

void SlashMenuState::moveSelection(int delta)
{
    if (m_items.isEmpty())
        return;
    const int count = int(m_items.size());
    m_selectedIndex = ((m_selectedIndex + delta) % count + count) % count;
}

void SlashMenuState::select(int index)
{
    if (index >= 0 && index < m_items.size())
        m_selectedIndex = index;
}

} // namespace wp
