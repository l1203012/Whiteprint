import WhiteprintCore

/// A flashcard deck while its editor is open: free-form title and cards,
/// including blank ones being filled in.
struct DeckDraft: Equatable {
    var title: String
    var cards: [Flashcard]

    init(_ deck: CardDeck) {
        title = deck.title ?? ""
        cards = deck.cards
    }

    /// The deck to save: trimmed title (nil when empty), blank cards left out,
    /// empty references dropped.
    var deck: CardDeck {
        let title = title.trimmingCharacters(in: .whitespacesAndNewlines)
        let cards = cards.compactMap { card -> Flashcard? in
            let question = card.question.trimmingCharacters(in: .whitespacesAndNewlines)
            let answer = card.answer.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !question.isEmpty || !answer.isEmpty else { return nil }
            let ref = card.ref?.trimmingCharacters(in: .whitespacesAndNewlines)
            return Flashcard(question: question, answer: answer, ref: ref?.isEmpty == false ? ref : nil)
        }
        return CardDeck(title: title.isEmpty ? nil : title, cards: cards)
    }

    /// Appends a blank card and returns its index.
    @discardableResult
    mutating func addCard() -> Int {
        cards.append(Flashcard(question: "", answer: ""))
        return cards.count - 1
    }

    mutating func removeCard(at index: Int) {
        guard cards.indices.contains(index) else { return }
        cards.remove(at: index)
    }

    /// Moves a card one place up (`-1`) or down (`+1`). Returns false at the ends.
    @discardableResult
    mutating func moveCard(at index: Int, by delta: Int) -> Bool {
        let target = index + delta
        guard cards.indices.contains(index), cards.indices.contains(target) else { return false }
        cards.swapAt(index, target)
        return true
    }
}
