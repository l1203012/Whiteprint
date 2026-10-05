import Foundation

/// A question/answer card for studying.
public struct Flashcard: Codable, Equatable, Hashable {
    public var question: String
    public var answer: String
    /// Where the answer comes from, e.g. `Lecture3.pptx · slide 14`.
    public var ref: String?

    public init(question: String, answer: String, ref: String? = nil) {
        self.question = question
        self.answer = answer
        self.ref = ref
    }
}

/// A deck of flashcards, stored in a note as a ```` ```cards id=c1 ```` block:
///
///     # TCP basics
///     Q: What does TCP guarantee?
///     A: Ordered, reliable delivery.
///     ref: Lecture3.pptx · slide 4
///
///     Q: …
///
/// Cards are separated by blank lines. Lines that don't start a field continue
/// the previous one, so questions and answers can span lines.
public struct CardDeck: Equatable {
    /// Stable id, unique within the note (e.g. `c2`).
    public var id: String
    public var title: String?
    public var cards: [Flashcard]

    public init(id: String = "", title: String? = nil, cards: [Flashcard]) {
        self.id = id
        self.title = title
        self.cards = cards
    }

    /// Parses the block's body. Lenient: unknown lines continue the previous field.
    public init(id: String, parsing source: String) {
        var title: String?
        var cards: [Flashcard] = []
        var current: Flashcard?
        var field: WritableKeyPath<Flashcard, String>?

        func finishCard() {
            if let card = current, !card.question.isEmpty || !card.answer.isEmpty {
                cards.append(card)
            }
            current = nil
            field = nil
        }

        for rawLine in source.split(separator: "\n", omittingEmptySubsequences: false) {
            let line = String(rawLine)
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            if trimmed.isEmpty {
                finishCard()
            } else if let question = Self.value(of: "Q:", in: trimmed) {
                finishCard()
                current = Flashcard(question: question, answer: "")
                field = \.question
            } else if let answer = Self.value(of: "A:", in: trimmed) {
                if current == nil { current = Flashcard(question: "", answer: "") }
                current?.answer = answer
                field = \.answer
            } else if let ref = Self.value(of: "ref:", in: trimmed), current != nil {
                current?.ref = ref.isEmpty ? nil : ref
                field = nil
            } else if title == nil, cards.isEmpty, current == nil, trimmed.hasPrefix("# ") {
                title = String(trimmed.dropFirst(2)).trimmingCharacters(in: .whitespaces)
            } else if let field, current != nil {
                let text = trimmed.hasPrefix("\\") ? String(trimmed.dropFirst()) : trimmed
                current![keyPath: field] += "\n" + text
            }
        }
        finishCard()
        self.init(id: id, title: title, cards: cards)
    }

    /// The block's body in the format described above.
    public var source: String {
        var parts: [String] = []
        if let title, !title.isEmpty {
            parts.append("# " + Self.singleLine(title))
        }
        for card in cards {
            var lines = ["Q: " + Self.escaped(card.question), "A: " + Self.escaped(card.answer)]
            if let ref = card.ref, !ref.isEmpty {
                lines.append("ref: " + Self.singleLine(ref))
            }
            parts.append(lines.joined(separator: "\n"))
        }
        return parts.joined(separator: "\n\n")
    }

    private static let fieldPrefixes = ["Q:", "A:", "ref:", "# "]

    private static func value(of prefix: String, in line: String) -> String? {
        guard line.hasPrefix(prefix) else { return nil }
        return String(line.dropFirst(prefix.count)).trimmingCharacters(in: .whitespaces)
    }

    /// Keeps a multi-line field from splitting the card: blank lines are dropped,
    /// and continuation lines that look like a field or a fence get a `\`.
    private static func escaped(_ text: String) -> String {
        let lines = text.split(separator: "\n", omittingEmptySubsequences: true)
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }
        guard let first = lines.first else { return "" }
        let rest = lines.dropFirst().map { line -> String in
            let looksStructural = fieldPrefixes.contains { line.hasPrefix($0) }
                || line.hasPrefix("\\") || line.hasPrefix("```") || line.hasPrefix("~~~")
            return looksStructural ? "\\" + line : line
        }
        return ([first] + rest).joined(separator: "\n")
    }

    private static func singleLine(_ text: String) -> String {
        text.split(whereSeparator: \.isNewline).joined(separator: " ")
    }
}
