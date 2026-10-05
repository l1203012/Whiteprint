import AppKit
import WhiteprintCore
import WhiteprintRender

/// TEMPORARY: read-only stand-in for a flashcard deck block until the real
/// deck view exists. Shows the questions and answers as muted text.
final class DeckPlaceholderView: NSTextField {
    init(deck: CardDeck, palette: BlueprintPalette) {
        super.init(frame: .zero)
        stringValue = Self.text(for: deck)
        isEditable = false
        isBordered = false
        drawsBackground = false
        textColor = palette.muted
        font = .systemFont(ofSize: 14)
        lineBreakMode = .byWordWrapping
        maximumNumberOfLines = 0
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    static func text(for deck: CardDeck) -> String {
        let header = "🗂 " + (deck.title ?? "Flashcards") + " · \(deck.cards.count) cards"
        return ([header] + deck.cards.map { "Q: \($0.question)\nA: \($0.answer)" }).joined(separator: "\n\n")
    }
}
