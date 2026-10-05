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
                }
            }.joined(separator: "\n\n")
        }
        var body = pageTexts.joined(separator: "\n\n---\n\n")
        if let title = frontMatter.title, !title.isEmpty, !body.hasPrefix("# \(title)") {
            body = body.isEmpty ? "# \(title)" : "# \(title)\n\n\(body)"
        }
        return body.isEmpty ? "" : body + "\n"
    }
}
