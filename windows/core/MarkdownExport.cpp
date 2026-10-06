#include "core/MarkdownExport.h"

namespace wp {

QString Note::markdown(const std::optional<QString> &drawingPlaceholder) const
{
    QStringList pageTexts;
    for (const NotePage &page : m_pages) {
        QStringList parts;
        for (const NoteBlock &block : page.blocks) {
            switch (block.kind) {
            case BlockKind::text: parts.append(block.text); break;
            case BlockKind::drawing:
                if (drawingPlaceholder)
                    parts.append(*drawingPlaceholder);
                break;
            case BlockKind::cards: parts.append(markdown(block.deck)); break;
            }
        }
        pageTexts.append(parts.join(QLatin1String("\n\n")));
    }
    QString body = pageTexts.join(QLatin1String("\n\n---\n\n"));
    if (const auto title = frontMatter.title(); title && !title->isEmpty()
        && !body.startsWith(QStringLiteral("# ") + *title)) {
        body = body.isEmpty() ? QStringLiteral("# ") + *title : QStringLiteral("# ") + *title + QStringLiteral("\n\n") + body;
    }
    return body.isEmpty() ? QString() : body + QLatin1Char('\n');
}

/// A deck as a Markdown list of bold questions with their answers.
QString Note::markdown(const CardDeck &deck)
{
    QStringList lines;
    if (deck.title && !deck.title->isEmpty())
        lines.append(QStringLiteral("**Flashcards: ") + *deck.title + QStringLiteral("**\n"));
    for (const Flashcard &card : deck.cards) {
        QString answer = card.answer;
        answer.replace(QLatin1Char('\n'), QLatin1String("\n  "));
        const QString ref = card.ref ? QStringLiteral(" *(") + *card.ref + QStringLiteral(")*") : QString();
        lines.append(QStringLiteral("- **") + card.question + QStringLiteral("**\n  ") + answer + ref);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace wp
