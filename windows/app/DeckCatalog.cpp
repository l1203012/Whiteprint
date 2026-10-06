#include "app/DeckCatalog.h"

#include "app/AppServices.h"
#include "core/TextUtil.h"

namespace wp::DeckCatalog {

QString Entry::title() const
{
    const QString trimmed = deck.title ? trimmedWhitespace(*deck.title) : QString();
    return trimmed.isEmpty() ? noteTitle : trimmed;
}

QList<Entry> entries(const QList<NoteEntry> &notes, const ProgressFn &progress, const QDateTime &now)
{
    QList<Entry> result;
    for (const NoteEntry &note : notes) {
        for (const CardDeck &deck : note.decks) {
            if (deck.cards.isEmpty())
                continue;
            result.append({note.url, note.title, deck, FlashcardSession::summary(deck, progress(note.url, deck.id), now)});
        }
    }
    return result;
}

QList<Entry> all()
{
    AppServices &services = AppServices::shared();
    FlashcardProgressStore &store = services.flashcards();
    return entries(services.library().entries(),
                   [&store](const QString &note, const QString &deck) { return store.progress(note, deck); });
}

} // namespace wp::DeckCatalog
