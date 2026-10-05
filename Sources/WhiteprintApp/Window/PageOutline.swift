import WhiteprintCore

/// Short labels for a note's pages, for the sidebar and the command palette.
enum PageOutline {
    /// The page's first heading (or first line of text), or `Page n`.
    static func title(of page: NotePage, number: Int) -> String {
        for block in page.blocks {
            guard case .text(let text) = block else { continue }
            let lines = text.split(separator: "\n").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
            let heading = lines.first { $0.hasPrefix("#") } ?? lines.first
            if let heading {
                let label = heading.drop { $0 == "#" }.trimmingCharacters(in: .whitespaces)
                if !label.isEmpty { return String(label.prefix(60)) }
            }
        }
        return "Page \(number)"
    }

    static func titles(of note: Note) -> [String] {
        note.pages.enumerated().map { title(of: $1, number: $0 + 1) }
    }
}
