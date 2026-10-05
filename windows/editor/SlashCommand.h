#pragma once
#include "render/MarkdownSyntax.h" // TextRange

#include <QList>
#include <QString>
#include <optional>

namespace wp {

/// The commands offered by the `/` menu.
enum class SlashCommand {
    heading1,
    heading2,
    heading3,
    bulletList,
    numberedList,
    checklist,
    quote,
    codeBlock,
    divider,
    drawing,
    flashcards,
    newPage
};

namespace SlashCommands {

/// Every command, in menu order.
QList<SlashCommand> all();
QString title(SlashCommand command);
QString subtitle(SlashCommand command);

/// Commands matching what was typed after the slash: word-prefix matches
/// on the title first, then keyword prefixes, then substrings.
QList<SlashCommand> matching(const QString &query);

} // namespace SlashCommands

/// What the open slash menu shows, driven by the text of the block it was
/// opened in. Pure, so the view only draws it.
class SlashMenuState
{
public:
    /// Where the `/` is in the block's text.
    explicit SlashMenuState(int slashLocation) : m_slashLocation(slashLocation), m_items(SlashCommands::all()) {}

    int slashLocation() const { return m_slashLocation; }
    const QString &query() const { return m_query; }
    const QList<SlashCommand> &items() const { return m_items; }
    int selectedIndex() const { return m_selectedIndex; }

    std::optional<SlashCommand> selected() const;

    /// The `/query` text, for removing it when a command is chosen.
    TextRange typedRange() const { return TextRange(m_slashLocation, 1 + int(m_query.size())); }

    /// Follows the text after an edit or caret move. Returns false when the
    /// menu should close: the slash was deleted, the caret left the query, or
    /// the query ends in a space and matches nothing.
    bool update(const QString &text, int caret);

    void moveSelection(int delta);
    void select(int index);

private:
    int m_slashLocation;
    QString m_query;
    QList<SlashCommand> m_items;
    int m_selectedIndex = 0;
};

} // namespace wp
