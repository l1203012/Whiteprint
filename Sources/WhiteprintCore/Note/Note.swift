/// A Whiteprint note: the in-memory form of a `.wprint` file.
///
/// A note is a list of pages. Each page is a list of blocks, where a block is
/// either Markdown text or a drawing written in the Whiteprint drawing language.
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

    /// Drawings with a missing, invalid or duplicate id get a fresh one.
    init(frontMatter: FrontMatter, pages: [NotePage]) {
        self.frontMatter = frontMatter
        self.pages = pages.isEmpty ? [NotePage()] : pages
        normalizeDrawingIDs()
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

    /// An unused drawing id (`d1`, `d2`, …). Ids are never reused within a note,
    /// even after a drawing is deleted, so a stale id held by Claude can't point
    /// at a different drawing. The high-water mark is kept in the front matter.
    public func nextDrawingID() -> String {
        "d\(highestDrawingNumber + 1)"
    }

    private var highestDrawingNumber: Int {
        max(Self.highestDrawingNumber(in: drawings.map(\.id)), frontMatter.lastDrawingNumber)
    }

    /// Records `id` as used so `nextDrawingID` never returns it again.
    mutating func reserveDrawingID(_ id: String) {
        let number = Self.highestDrawingNumber(in: [id])
        if number > frontMatter.lastDrawingNumber {
            frontMatter.lastDrawingNumber = number
        }
    }

    /// Gives every drawing a valid, unique id, keeping existing ones where possible.
    mutating func normalizeDrawingIDs() {
        var used = Set<String>()
        var missing: [(page: Int, block: Int)] = []
        for p in pages.indices {
            for b in pages[p].blocks.indices {
                guard case .drawing(let drawing) = pages[p].blocks[b] else { continue }
                if Drawing.isValidID(drawing.id) && !used.contains(drawing.id) {
                    used.insert(drawing.id)
                } else {
                    missing.append((p, b))
                }
            }
        }
        var next = max(Self.highestDrawingNumber(in: used), frontMatter.lastDrawingNumber) + 1
        for (p, b) in missing {
            guard case .drawing(var drawing) = pages[p].blocks[b] else { continue }
            drawing.id = "d\(next)"
            next += 1
            pages[p].blocks[b] = .drawing(drawing)
        }
        for id in drawings.map(\.id) {
            reserveDrawingID(id)
        }
    }

    private static func highestDrawingNumber<S: Sequence>(in ids: S) -> Int where S.Element == String {
        ids.compactMap { id in id.hasPrefix("d") ? Int(id.dropFirst()) : nil }.max() ?? 0
    }
}

public struct NotePage: Equatable {
    public var blocks: [NoteBlock]

    public init(blocks: [NoteBlock] = []) {
        self.blocks = blocks
    }
}

public enum NoteBlock: Equatable {
    /// Markdown text. Never contains a top-level ```` ```wp ```` fence or a `+++page` line.
    case text(String)
    case drawing(Drawing)
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

    public var formatVersion: Int? {
        self[Self.versionKey].flatMap { Int($0) }
    }

    static let lastDrawingKey = "last-drawing"

    /// Highest `dN` drawing number ever handed out in this note.
    var lastDrawingNumber: Int {
        get { self[Self.lastDrawingKey].flatMap { Int($0) } ?? 0 }
        set { self[Self.lastDrawingKey] = newValue > 0 ? String(newValue) : nil }
    }
}
