import Foundation

public enum NoteFormatError: Error, Equatable {
    /// The file was written by a newer Whiteprint.
    case unsupportedVersion(Int)
    /// The `whiteprint:` field isn't a number.
    case invalidVersion(String)
}

// MARK: - Parsing

extension Note {
    static let pageSeparator = "+++page"
    static let drawingFenceInfo = "wp"
    static let cardsFenceInfo = "cards"
    static let blockFenceInfos: Set<Substring> = [Substring(drawingFenceInfo), Substring(cardsFenceInfo)]

    /// Parses `.wprint` text. Parsing is lenient: anything that isn't front matter,
    /// a page separator or a drawing is kept as Markdown, and an unterminated
    /// drawing fence runs to the end of the file. Only an unreadable version fails.
    public init(parsing text: String) throws {
        let normalized = text
            .replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
        var lines = normalized.split(separator: "\n", omittingEmptySubsequences: false)[...]

        var frontMatter = FrontMatter()
        if lines.first.map(Self.trimTrailing) == "---",
           let close = lines.dropFirst().firstIndex(where: { Self.trimTrailing($0) == "---" }) {
            frontMatter = FrontMatter(fields: Self.parseFields(lines[(lines.startIndex + 1)..<close]))
            lines = lines[(close + 1)...]
        }

        let rawVersion = frontMatter[FrontMatter.versionKey] ?? ""
        guard let version = Int(rawVersion.trimmingCharacters(in: .whitespaces)) else {
            throw NoteFormatError.invalidVersion(rawVersion)
        }
        guard version <= Note.formatVersion else {
            throw NoteFormatError.unsupportedVersion(version)
        }

        self.init(frontMatter: frontMatter, pages: Self.parsePages(lines))
    }

    /// Parses `.wprint` body text (no front matter) into pages. Drawing ids
    /// are kept as written, or empty when missing.
    static func parseBody(_ text: String) -> [NotePage] {
        let normalized = text
            .replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
        return parsePages(normalized.split(separator: "\n", omittingEmptySubsequences: false)[...])
    }

    private static func parsePages(_ lines: ArraySlice<Substring>) -> [NotePage] {
        var body = BodyParser()
        for line in lines {
            body.consume(line)
        }
        return body.finish()
    }

    private static func parseFields(_ lines: ArraySlice<Substring>) -> [FrontMatter.Field] {
        lines.compactMap { line in
            guard let colon = line.firstIndex(of: ":") else { return nil }
            let key = line[..<colon].trimmingCharacters(in: .whitespaces)
            let value = line[line.index(after: colon)...].trimmingCharacters(in: .whitespaces)
            return key.isEmpty ? nil : FrontMatter.Field(key: key, value: value)
        }
    }

    static func trimTrailing(_ line: Substring) -> Substring {
        var line = line
        while let last = line.last, last == " " || last == "\t" {
            line.removeLast()
        }
        return line
    }
}

/// Splits the body (everything after the front matter) into pages and blocks.
private struct BodyParser {
    private var pages: [NotePage] = []
    private var blocks: [NoteBlock] = []
    private var text: [Substring] = []
    private var fence: MarkdownFence?
    /// An open ```` ```wp ```` or ```` ```cards ```` block.
    private var special: (info: Substring, id: String, lines: [Substring])?

    mutating func consume(_ line: Substring) {
        if let open = fence {
            if open.isClosed(by: line) {
                fence = nil
                if special != nil {
                    finishSpecialBlock()
                    return
                }
            }
            if special != nil {
                special!.lines.append(line)
            } else {
                text.append(line)
            }
            return
        }

        if Note.trimTrailing(line) == Note.pageSeparator {
            finishPage()
            return
        }

        if let open = MarkdownFence.opening(line) {
            fence = open
            if let info = open.infoWords.first, Note.blockFenceInfos.contains(info) {
                flushText()
                // A missing id is filled in by `normalizeBlockIDs`.
                special = (info, open.attribute("id") ?? "", [])
                return
            }
        }
        text.append(line)
    }

    mutating func finish() -> [NotePage] {
        if special != nil {
            finishSpecialBlock()
        }
        finishPage()
        return pages
    }

    private mutating func finishSpecialBlock() {
        guard let special else { return }
        let source = special.lines.joined(separator: "\n")
        if special.info == Substring(Note.cardsFenceInfo) {
            blocks.append(.cards(CardDeck(id: special.id, parsing: source)))
        } else {
            blocks.append(.drawing(Drawing(id: special.id, source: source)))
        }
        self.special = nil
    }

    private mutating func finishPage() {
        flushText()
        pages.append(NotePage(blocks: blocks))
        blocks = []
    }

    /// Adds the buffered lines as a text block, without surrounding blank lines.
    private mutating func flushText() {
        defer { text = [] }
        let isBlank: (Substring) -> Bool = { $0.allSatisfy(\.isWhitespace) }
        guard let first = text.firstIndex(where: { !isBlank($0) }),
              let last = text.lastIndex(where: { !isBlank($0) }) else { return }
        blocks.append(.text(text[first...last].joined(separator: "\n")))
    }
}

// MARK: - Serializing

extension Note {
    /// Writes canonical `.wprint` text: front matter, then pages separated by
    /// `+++page`, with blocks separated by one blank line.
    public func serialized() -> String {
        var out = "---\n"
        for field in frontMatter.fields {
            out += "\(field.key): \(field.value)\n"
        }
        out += "---\n"
        let body = pages.map(Self.serialize).joined(separator: "\n\n\(Self.pageSeparator)\n\n")
        if !body.isEmpty {
            out += "\n" + body + "\n"
        }
        return out
    }

    /// One page's blocks in `.wprint` syntax, separated by blank lines.
    static func serialize(_ page: NotePage) -> String {
        page.blocks.map(serialize).joined(separator: "\n\n")
    }

    private static func serialize(_ block: NoteBlock) -> String {
        switch block {
        case .text(let text):
            let text = escapingStructure(in: text)
            // Close a fence the text left open, so it can't swallow the blocks after it.
            guard let open = MarkdownFence.unclosed(in: text) else { return text }
            return text + "\n" + String(repeating: open.marker, count: open.length)
        case .drawing(let drawing):
            return fenced(drawing.source, info: drawingFenceInfo, id: drawing.id)
        case .cards(let deck):
            return fenced(deck.source, info: cardsFenceInfo, id: deck.id)
        }
    }

    private static func fenced(_ source: String, info: String, id: String) -> String {
        let fence = String(repeating: "`", count: fenceLength(for: source))
        let content = source.isEmpty ? "" : source + "\n"
        return "\(fence)\(info) id=\(id)\n\(content)\(fence)"
    }

    /// Text typed by someone can contain a line that would read back as
    /// structure: a `+++page` separator or a ```` ```wp ```` / ```` ```cards ````
    /// opener outside a code block. Such lines get a leading `\`, which keeps
    /// them literal in Markdown too.
    private static func escapingStructure(in text: String) -> String {
        var open: MarkdownFence?
        var changed = false
        let lines = text.split(separator: "\n", omittingEmptySubsequences: false).map { line -> Substring in
            if let fence = open {
                if fence.isClosed(by: line) { open = nil }
                return line
            }
            if trimTrailing(line) == pageSeparator {
                changed = true
                return "\\" + line
            }
            if let fence = MarkdownFence.opening(line) {
                if let info = fence.infoWords.first, blockFenceInfos.contains(info) {
                    changed = true
                    return "\\" + line
                }
                open = fence
            }
            return line
        }
        return changed ? lines.joined(separator: "\n") : text
    }

    /// One backtick longer than any backtick run that starts a line of the source.
    private static func fenceLength(for source: String) -> Int {
        let longest = source.split(separator: "\n").map { (line: Substring) -> Int in
            line.drop { $0 == " " }.prefix { $0 == "`" }.count
        }.max() ?? 0
        return max(3, longest + 1)
    }
}
