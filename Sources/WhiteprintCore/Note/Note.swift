import Foundation

/// A Whiteprint note: the in-memory form of a `.wprint` file.
///
/// A note is a list of pages. Each page is a list of blocks: Markdown text,
/// drawings written in the Whiteprint drawing language, or flashcard decks.
public struct Note: Equatable {
    /// The newest `.wprint` format version this build can read and write.
    public static let formatVersion = 1

    public var frontMatter: FrontMatter
    /// Always contains at least one page.
    public var pages: [NotePage] {
        didSet { if pages.isEmpty { pages = [NotePage()] } }
    }

    public init(title: String? = nil, pages: [NotePage] = [NotePage()]) {
        var frontMatter = FrontMatter()
        frontMatter.title = title
        self.init(frontMatter: frontMatter, pages: pages)
    }

    /// Drawings and decks with a missing, invalid or duplicate id get a fresh one.
    init(frontMatter: FrontMatter, pages: [NotePage]) {
        self.frontMatter = frontMatter
        self.pages = pages.isEmpty ? [NotePage()] : pages
        normalizeBlockIDs()
    }

    /// Every drawing in the note, in reading order.
    public var drawings: [Drawing] {
        pages.flatMap { page in
            page.blocks.compactMap { block -> Drawing? in
                if case .drawing(let drawing) = block { return drawing }
                return nil
            }
        }
    }

    /// Every flashcard deck in the note, in reading order.
    public var decks: [CardDeck] {
        pages.flatMap { page in
            page.blocks.compactMap { block -> CardDeck? in
                if case .cards(let deck) = block { return deck }
                return nil
            }
        }
    }

    /// An unused drawing id (`d1`, `d2`, …). Ids are never reused within a note,
    /// even after a drawing is deleted, so a stale id held by Claude can't point
    /// at a different drawing. The high-water mark is kept in the front matter.
    public func nextDrawingID() -> String {
        nextID(.drawing)
    }

    /// An unused deck id (`c1`, `c2`, …), never reused like drawing ids.
    public func nextDeckID() -> String {
        nextID(.cards)
    }

    func nextID(_ kind: BlockIDKind) -> String {
        "\(kind.prefix)\(highestNumber(kind) + 1)"
    }

    private func highestNumber(_ kind: BlockIDKind) -> Int {
        max(kind.highestNumber(in: ids(of: kind)), frontMatter.lastNumber(kind))
    }

    private func ids(of kind: BlockIDKind) -> [String] {
        pages.flatMap { $0.blocks.compactMap { $0.idKind == kind ? $0.id : nil } }
    }

    /// Records `id` as used so it's never handed out again.
    mutating func reserveID(_ id: String) {
        guard let kind = BlockIDKind(id: id) else { return }
        let number = kind.highestNumber(in: [id])
        if number > frontMatter.lastNumber(kind) {
            frontMatter.setLastNumber(number, kind)
        }
    }

    /// Gives every drawing and deck a valid, unique id, keeping existing ones where possible.
    mutating func normalizeBlockIDs() {
        for kind in BlockIDKind.allCases {
            var used = Set<String>()
            var missing: [(page: Int, block: Int)] = []
            for p in pages.indices {
                for b in pages[p].blocks.indices where pages[p].blocks[b].idKind == kind {
                    let id = pages[p].blocks[b].id ?? ""
                    if Drawing.isValidID(id) && !used.contains(id) {
                        used.insert(id)
                    } else {
                        missing.append((p, b))
                    }
                }
            }
            var next = max(kind.highestNumber(in: used), frontMatter.lastNumber(kind)) + 1
            for (p, b) in missing {
                pages[p].blocks[b] = pages[p].blocks[b].withID("\(kind.prefix)\(next)")
                next += 1
            }
            for id in ids(of: kind) {
                reserveID(id)
            }
        }
    }
}

/// Blocks that carry a stable id: drawings (`d1`) and flashcard decks (`c1`).
enum BlockIDKind: CaseIterable {
    case drawing, cards

    init?(id: String) {
        switch id.first {
        case "d": self = .drawing
        case "c": self = .cards
        default: return nil
        }
    }

    var prefix: String {
        switch self {
        case .drawing: return "d"
        case .cards: return "c"
        }
    }

    func highestNumber<S: Sequence>(in ids: S) -> Int where S.Element == String {
        ids.compactMap { id in id.hasPrefix(prefix) ? Int(id.dropFirst()) : nil }.max() ?? 0
    }
}

public struct NotePage: Equatable {
    public var blocks: [NoteBlock]

    public init(blocks: [NoteBlock] = []) {
        self.blocks = blocks
    }
}

public enum NoteBlock: Equatable {
    /// Markdown text. Never contains a top-level ```` ```wp ```` or ```` ```cards ````
    /// fence or a `+++page` line.
    case text(String)
    case drawing(Drawing)
    case cards(CardDeck)

    /// The block's stable id; nil for text.
    public var id: String? {
        switch self {
        case .text: return nil
        case .drawing(let drawing): return drawing.id
        case .cards(let deck): return deck.id
        }
    }

    var idKind: BlockIDKind? {
        switch self {
        case .text: return nil
        case .drawing: return .drawing
        case .cards: return .cards
        }
    }

    func withID(_ id: String) -> NoteBlock {
        switch self {
        case .text: return self
        case .drawing(var drawing):
            drawing.id = id
            return .drawing(drawing)
        case .cards(var deck):
            deck.id = id
            return .cards(deck)
        }
    }
}

public struct Drawing: Equatable {
    /// Stable id, unique within the note (e.g. `d3`). Used by the MCP tools.
    public var id: String
    /// Drawing language source, stored verbatim.
    public var source: String

    public init(id: String, source: String) {
        self.id = id
        self.source = source
    }

    static func isValidID(_ id: String) -> Bool {
        !id.isEmpty && id.allSatisfy { $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "_" || $0 == "-") }
    }
}

/// The `key: value` header at the top of a `.wprint` file. Field order is preserved.
public struct FrontMatter: Equatable {
    public struct Field: Equatable {
        public var key: String
        public var value: String
    }

    static let versionKey = "whiteprint"

    public private(set) var fields: [Field]

    public init() {
        self.init(fields: [])
    }

    /// The format version field is always present and always first.
    init(fields: [Field]) {
        var fields = fields
        if let i = fields.firstIndex(where: { $0.key == Self.versionKey }) {
            fields.insert(fields.remove(at: i), at: 0)
        } else {
            fields.insert(Field(key: Self.versionKey, value: String(Note.formatVersion)), at: 0)
        }
        self.fields = fields
    }

    public subscript(key: String) -> String? {
        get { fields.first { $0.key == key }?.value }
        set {
            guard key != Self.versionKey else { return }
            if let newValue {
                // Values are single-line by definition.
                let value = newValue.replacingOccurrences(of: "\n", with: " ")
                if let i = fields.firstIndex(where: { $0.key == key }) {
                    fields[i].value = value
                } else {
                    fields.append(Field(key: key, value: value))
                }
            } else {
                fields.removeAll { $0.key == key }
            }
        }
    }

    public var title: String? {
        get { self["title"] }
        set { self["title"] = newValue }
    }

    /// The page icon shown above the title, Notion-style: one emoji, stored
    /// as `icon: 🧭`. Setting an empty value removes the field.
    public var icon: String? {
        get { self["icon"].flatMap(Self.nonEmpty) }
        set { self["icon"] = newValue.flatMap(Self.nonEmpty) }
    }

    /// The banner across the top of the first page: a cover gallery id such
    /// as `monet-water-lilies`, stored as `cover: …`. Setting an empty value
    /// removes the field.
    public var cover: String? {
        get { self["cover"].flatMap(Self.nonEmpty) }
        set { self["cover"] = newValue.flatMap(Self.nonEmpty) }
    }

    private static func nonEmpty(_ value: String) -> String? {
        let trimmed = value.trimmingCharacters(in: .whitespaces)
        return trimmed.isEmpty ? nil : trimmed
    }

    public var formatVersion: Int? {
        self[Self.versionKey].flatMap { Int($0) }
    }

    /// Highest id number ever handed out in this note for that kind of block,
    /// stored as `last-drawing` / `last-cards`.
    func lastNumber(_ kind: BlockIDKind) -> Int {
        self[Self.lastNumberKey(kind)].flatMap { Int($0) } ?? 0
    }

    mutating func setLastNumber(_ number: Int, _ kind: BlockIDKind) {
        self[Self.lastNumberKey(kind)] = number > 0 ? String(number) : nil
    }

    private static func lastNumberKey(_ kind: BlockIDKind) -> String {
        switch kind {
        case .drawing: return "last-drawing"
        case .cards: return "last-cards"
        }
    }
}
