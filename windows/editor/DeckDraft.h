#pragma once
#include "core/CardDeck.h"

#include <QList>
#include <QString>

namespace wp {

/// A flashcard deck while its editor is open: free-form title and cards,
/// including blank ones being filled in.
struct DeckDraft {
    QString title;
    QList<Flashcard> cards;

    DeckDraft() = default;
    explicit DeckDraft(const CardDeck &deck) : title(deck.title.value_or(QString())), cards(deck.cards) {}

    /// The deck to save: trimmed title (nullopt when empty), blank cards left out,
    /// empty references dropped.
    CardDeck deck() const
    {
        const QString trimmedTitle = title.trimmed();
        QList<Flashcard> kept;
        for (const Flashcard &card : cards) {
            const QString question = card.question.trimmed();
            const QString answer = card.answer.trimmed();
            if (question.isEmpty() && answer.isEmpty())
                continue;
            std::optional<QString> ref;
            if (card.ref) {
                const QString trimmedRef = card.ref->trimmed();
                if (!trimmedRef.isEmpty())
                    ref = trimmedRef;
            }
            kept.append(Flashcard(question, answer, ref));
        }
        return CardDeck(QString(), trimmedTitle.isEmpty() ? std::nullopt : std::optional<QString>(trimmedTitle), kept);
    }

    /// Appends a blank card and returns its index.
    int addCard()
    {
        cards.append(Flashcard(QString(), QString()));
        return int(cards.size()) - 1;
    }

    void removeCard(int index)
    {
        if (index >= 0 && index < cards.size())
            cards.removeAt(index);
    }

    /// Moves a card one place up (`-1`) or down (`+1`). Returns false at the ends.
    bool moveCard(int index, int delta)
    {
        const int target = index + delta;
        if (index < 0 || index >= cards.size() || target < 0 || target >= cards.size())
            return false;
        cards.swapItemsAt(index, target);
        return true;
    }
};

} // namespace wp
