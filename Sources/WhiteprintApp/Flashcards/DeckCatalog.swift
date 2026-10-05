import Foundation
import WhiteprintCore

extension Notification.Name {
    /// Posted after a card is rated, so due counts refresh.
    static let flashcardProgressDidChange = Notification.Name("WhiteprintFlashcardProgressDidChange")
}

/// Every flashcard deck across the notes, with what's left to study.
enum DeckCatalog {
    struct Entry {
        var note: URL
        var noteTitle: String
        var deck: CardDeck
        var summary: FlashcardSession.Summary

        /// The deck's title, or its note's.
        var title: String {
            let title = deck.title?.trimmingCharacters(in: .whitespaces) ?? ""
            return title.isEmpty ? noteTitle : title
        }
    }

    /// Decks with at least one card, in note order.
    static func entries(
        in notes: [NoteEntry], progress: (URL, String) -> [String: CardProgress], now: Date = Date()
    ) -> [Entry] {
        notes.flatMap { note in
            note.decks.filter { !$0.cards.isEmpty }.map { deck in
                Entry(note: note.url, noteTitle: note.title, deck: deck,
                      summary: FlashcardSession.summary(of: deck, progress: progress(note.url, deck.id), now: now))
            }
        }
    }

    /// Every deck in the notes library.
    static func all() -> [Entry] {
        let store = AppServices.shared.flashcards
        return entries(in: AppServices.shared.library.entries) { store.progress(note: $0, deck: $1) }
    }
}
