/// Edits used by the MCP tools and the editor. Page numbers are 1-based here,
/// matching what Claude sees in `read_note`.
public enum NoteEditError: Error, Equatable, CustomStringConvertible {
    case pageOutOfRange(Int, pageCount: Int)
    case unknownDrawing(String)
    case unknownDeck(String)
    case lastPage

    public var description: String {
        switch self {
        case let .pageOutOfRange(page, count):
            return "page \(page) doesn't exist (note has \(count) page\(count == 1 ? "" : "s"))"
        case let .unknownDrawing(id):
            return "no drawing '\(id)' in this note"
        case let .unknownDeck(id):
            return "no flashcard deck '\(id)' in this note"
        case .lastPage:
            return "can't remove the only page"
        }
    }
}

public enum WriteMode: String, Codable, Equatable {
    case append, replace
}

extension Note {
    /// The page's content in `.wprint` syntax, drawings included with their ids.
    public func pageSource(_ page: Int) throws -> String {
        Self.serialize(pages[try index(of: page)])
    }

    /// Writes `.wprint` body text to a page. The text may contain ```` ```wp ````
    /// drawings and `+++page` separators; extra pages are inserted after this one.
    ///
    /// New drawings get fresh ids. With `.replace`, a drawing written back with
    /// an id it had on this page keeps that id, so read → edit → write is stable.
    public mutating func write(_ text: String, page: Int, mode: WriteMode) throws {
        let i = try index(of: page)
        var written = Self.parseBody(text)
        let replacedIDs: Set<String> = mode == .replace ? Set(Self.blockIDs(in: [pages[i]])) : []
        let keptElsewhere = Set(Self.blockIDs(in: pages.enumerated().filter { $0.offset != i }.map(\.element)))
        var claimed = Set<String>()
        for p in written.indices {
            for b in written[p].blocks.indices {
                guard let id = written[p].blocks[b].id else { continue }
                if replacedIDs.contains(id) && !keptElsewhere.contains(id) && !claimed.contains(id) {
                    claimed.insert(id)
                } else {
                    written[p].blocks[b] = written[p].blocks[b].withID("")
                }
            }
        }

        switch mode {
        case .append:
            pages[i].blocks += written[0].blocks
        case .replace:
            pages[i].blocks = written[0].blocks
        }
        pages.insert(contentsOf: written.dropFirst(), at: i + 1)
        normalizeBlockIDs()
    }

    /// Inserts a drawing at the end of the page, or right after the drawing or
    /// deck `after`. Returns the new drawing's id.
    @discardableResult
    public mutating func insertDrawing(_ source: String, page: Int, after: String? = nil) throws -> String {
        try insert(.drawing(Drawing(id: nextDrawingID(), source: source)), page: page, after: after)
    }

    public mutating func updateDrawing(_ id: String, source: String) throws {
        let (p, b) = try location(of: id, .drawing)
        pages[p].blocks[b] = .drawing(Drawing(id: id, source: source))
    }

    public mutating func deleteDrawing(_ id: String) throws {
        let (p, b) = try location(of: id, .drawing)
        reserveID(id)
        pages[p].blocks.remove(at: b)
    }

    /// The drawing with `id`, and the 1-based page it's on.
    public func drawing(_ id: String) -> (drawing: Drawing, page: Int)? {
        guard let (p, b) = try? location(of: id, .drawing), case .drawing(let drawing) = pages[p].blocks[b] else {
            return nil
        }
        return (drawing, p + 1)
    }

    /// Inserts a flashcard deck at the end of the page, or right after the
    /// drawing or deck `after`. The deck's own id is ignored; returns the new one.
    @discardableResult
    public mutating func insertDeck(_ deck: CardDeck, page: Int, after: String? = nil) throws -> String {
        var deck = deck
        deck.id = nextDeckID()
        return try insert(.cards(deck), page: page, after: after)
    }

    /// Replaces a deck's title and cards, keeping its id.
    public mutating func updateDeck(_ id: String, _ deck: CardDeck) throws {
        let (p, b) = try location(of: id, .cards)
        var deck = deck
        deck.id = id
        pages[p].blocks[b] = .cards(deck)
    }

    public mutating func deleteDeck(_ id: String) throws {
        let (p, b) = try location(of: id, .cards)
        reserveID(id)
        pages[p].blocks.remove(at: b)
    }

    /// The deck with `id`, and the 1-based page it's on.
    public func deck(_ id: String) -> (deck: CardDeck, page: Int)? {
        guard let (p, b) = try? location(of: id, .cards), case .cards(let deck) = pages[p].blocks[b] else {
            return nil
        }
        return (deck, p + 1)
    }

    private mutating func insert(_ block: NoteBlock, page: Int, after: String?) throws -> String {
        let i = try index(of: page)
        if let after {
            guard let b = pages[i].blocks.firstIndex(where: { $0.id == after }) else {
                throw after.hasPrefix("c") ? NoteEditError.unknownDeck(after) : NoteEditError.unknownDrawing(after)
            }
            pages[i].blocks.insert(block, at: b + 1)
        } else {
            pages[i].blocks.append(block)
        }
        let id = block.id ?? ""
        reserveID(id)
        return id
    }

    /// Adds an empty page after `after` (default: at the end). Returns its number.
    @discardableResult
    public mutating func addPage(after: Int? = nil) throws -> Int {
        let i = try after.map { try index(of: $0) + 1 } ?? pages.count
        pages.insert(NotePage(), at: i)
        return i + 1
    }

    public mutating func removePage(_ page: Int) throws {
        let i = try index(of: page)
        guard pages.count > 1 else { throw NoteEditError.lastPage }
        for id in Self.blockIDs(in: [pages[i]]) {
            reserveID(id)
        }
        pages.remove(at: i)
    }

    // MARK: Helpers

    private func index(of page: Int) throws -> Int {
        guard (1...pages.count).contains(page) else {
            throw NoteEditError.pageOutOfRange(page, pageCount: pages.count)
        }
        return page - 1
    }

    private func location(of id: String, _ kind: BlockIDKind) throws -> (page: Int, block: Int) {
        for p in pages.indices {
            if let b = pages[p].blocks.firstIndex(where: { $0.idKind == kind && $0.id == id }) {
                return (p, b)
            }
        }
        throw kind == .cards ? NoteEditError.unknownDeck(id) : NoteEditError.unknownDrawing(id)
    }

    private static func blockIDs(in pages: [NotePage]) -> [String] {
        pages.flatMap { page in page.blocks.compactMap(\.id) }
    }
}
