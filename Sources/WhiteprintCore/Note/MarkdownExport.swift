extension Note {
    /// Plain Markdown for export. Drawings are left out (or replaced by
    /// `drawingPlaceholder`), front matter is dropped, and pages are separated
    /// by a horizontal rule. The title becomes an H1 unless the note already
    /// starts with that heading.
    public func markdown(drawingPlaceholder: String? = "*[drawing omitted]*") -> String {
        let pageTexts = pages.map { page -> String in
            page.blocks.compactMap { block -> String? in
                switch block {
                case .text(let text): return text
                case .drawing: return drawingPlaceholder
                case .cards(let deck): return Self.markdown(deck)
                }
            }.joined(separator: "\n\n")
        }
        var body = pageTexts.joined(separator: "\n\n---\n\n")
        if let title = frontMatter.title, !title.isEmpty, !body.hasPrefix("# \(title)") {
            body = body.isEmpty ? "# \(title)" : "# \(title)\n\n\(body)"
        }
        return body.isEmpty ? "" : body + "\n"
    }

    /// A deck as a Markdown list of bold questions with their answers.
    private static func markdown(_ deck: CardDeck) -> String {
        var lines: [String] = []
        if let title = deck.title, !title.isEmpty {
            lines.append("**Flashcards: \(title)**\n")
        }
        for card in deck.cards {
            let answer = card.answer.replacingOccurrences(of: "\n", with: "\n  ")
            let ref = card.ref.map { " *(\($0))*" } ?? ""
            lines.append("- **\(card.question)**\n  \(answer)\(ref)")
        }
        return lines.joined(separator: "\n")
    }
}
