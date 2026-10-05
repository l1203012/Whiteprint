/// Edits used by the MCP tools and the editor. Page numbers are 1-based here,
/// matching what Claude sees in `read_note`.
public enum NoteEditError: Error, Equatable, CustomStringConvertible {
    case pageOutOfRange(Int, pageCount: Int)
    case unknownDrawing(String)
    case lastPage

    public var description: String {
        switch self {
        case let .pageOutOfRange(page, count):
            return "page \(page) doesn't exist (note has \(count) page\(count == 1 ? "" : "s"))"
        case let .unknownDrawing(id):
            return "no drawing '\(id)' in this note"
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
        let replacedIDs: Set<String> = mode == .replace ? Set(Self.drawingIDs(in: [pages[i]])) : []
        let keptElsewhere = Set(Self.drawingIDs(in: pages.enumerated().filter { $0.offset != i }.map(\.element)))
        var claimed = Set<String>()
        for p in written.indices {
            for b in written[p].blocks.indices {
                guard case .drawing(var drawing) = written[p].blocks[b] else { continue }
                let keep = replacedIDs.contains(drawing.id) && !keptElsewhere.contains(drawing.id)
                    && !claimed.contains(drawing.id)
                if keep {
                    claimed.insert(drawing.id)
                } else {
                    drawing.id = ""
                }
                written[p].blocks[b] = .drawing(drawing)
            }
        }

        switch mode {
        case .append:
            pages[i].blocks += written[0].blocks
        case .replace:
            pages[i].blocks = written[0].blocks
        }
        pages.insert(contentsOf: written.dropFirst(), at: i + 1)
        normalizeDrawingIDs()
    }

    /// Inserts a drawing at the end of the page, or right after drawing `after`.
    /// Returns the new drawing's id.
    @discardableResult
    public mutating func insertDrawing(_ source: String, page: Int, after: String? = nil) throws -> String {
        let i = try index(of: page)
        let drawing = Drawing(id: nextDrawingID(), source: source)
        if let after {
            guard let b = pages[i].blocks.firstIndex(where: { Self.isDrawing($0, id: after) }) else {
                throw NoteEditError.unknownDrawing(after)
            }
            pages[i].blocks.insert(.drawing(drawing), at: b + 1)
        } else {
            pages[i].blocks.append(.drawing(drawing))
        }
        reserveDrawingID(drawing.id)
        return drawing.id
    }

    public mutating func updateDrawing(_ id: String, source: String) throws {
        let (p, b) = try location(ofDrawing: id)
        pages[p].blocks[b] = .drawing(Drawing(id: id, source: source))
    }

    public mutating func deleteDrawing(_ id: String) throws {
        let (p, b) = try location(ofDrawing: id)
        reserveDrawingID(id)
        pages[p].blocks.remove(at: b)
    }

    /// The drawing with `id`, and the 1-based page it's on.
    public func drawing(_ id: String) -> (drawing: Drawing, page: Int)? {
        guard let (p, b) = try? location(ofDrawing: id), case .drawing(let drawing) = pages[p].blocks[b] else {
            return nil
        }
        return (drawing, p + 1)
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
        for id in Self.drawingIDs(in: [pages[i]]) {
            reserveDrawingID(id)
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

    private func location(ofDrawing id: String) throws -> (page: Int, block: Int) {
        for p in pages.indices {
            if let b = pages[p].blocks.firstIndex(where: { Self.isDrawing($0, id: id) }) {
                return (p, b)
            }
        }
        throw NoteEditError.unknownDrawing(id)
    }

    private static func isDrawing(_ block: NoteBlock, id: String) -> Bool {
        if case .drawing(let drawing) = block { return drawing.id == id }
        return false
    }

    private static func drawingIDs(in pages: [NotePage]) -> [String] {
        pages.flatMap { page in
            page.blocks.compactMap { block -> String? in
                if case .drawing(let drawing) = block { return drawing.id }
                return nil
            }
        }
    }
}
