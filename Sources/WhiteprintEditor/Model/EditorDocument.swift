import Foundation
import WhiteprintCore

/// Identity of a block while a note is open in the editor. Views are keyed by
/// it, so a block keeps its view (and its typing undo) across edits.
struct BlockID: Hashable, CustomStringConvertible {
    let rawValue: Int
    var description: String { "#\(rawValue)" }
}

/// Identity of a page while a note is open in the editor.
struct PageID: Hashable {
    let rawValue: Int
}

struct EditorBlock: Equatable {
    enum Content: Equatable {
        case text(String)
        case drawing(Drawing)
    }

    var id: BlockID
    var content: Content

    var text: String? {
        if case .text(let text) = content { return text }
        return nil
    }

    var drawing: Drawing? {
        if case .drawing(let drawing) = content { return drawing }
        return nil
    }

    var isText: Bool { text != nil }
}

struct EditorPage: Equatable {
    var id: PageID
    var blocks: [EditorBlock]
}

/// A block's position: 0-based page and block indices.
struct BlockLocation: Equatable {
    var page: Int
    var block: Int
}

/// The editor's model of a note: pages of identified blocks.
///
/// It differs from `Note` in one way: every page ends with a text block, so
/// there's always somewhere to type (an empty page is one empty text block).
/// Empty text blocks are dropped again when converting back to a `Note`.
/// Text offsets are UTF-16, matching `NSTextView`.
struct EditorDocument: Equatable {
    private(set) var pages: [EditorPage]
    /// Front matter and the drawing id high-water mark; its pages are unused.
    private var base: Note
    private var lastID = 0

    init(note: Note) {
        self.init(note: note, lastID: 0)
    }

    /// The note as it would be saved.
    var note: Note {
        var note = base
        note.pages = pages.map { page in
            NotePage(blocks: page.blocks.compactMap { block in
                switch block.content {
                case .text(let text):
                    let canonical = Self.canonical(text)
                    return canonical.isEmpty ? nil : .text(canonical)
                case .drawing(let drawing):
                    return .drawing(drawing)
                }
            })
        }
        return note
    }

    // MARK: Lookup

    subscript(location: BlockLocation) -> EditorBlock {
        pages[location.page].blocks[location.block]
    }

    func location(of id: BlockID) -> BlockLocation? {
        for p in pages.indices {
            if let b = pages[p].blocks.firstIndex(where: { $0.id == id }) {
                return BlockLocation(page: p, block: b)
            }
        }
        return nil
    }

    func block(_ id: BlockID) -> EditorBlock? {
        location(of: id).map { self[$0] }
    }

    /// The next block in reading order, across pages.
    func block(after id: BlockID) -> EditorBlock? {
        guard let at = location(of: id) else { return nil }
        if at.block + 1 < pages[at.page].blocks.count {
            return pages[at.page].blocks[at.block + 1]
        }
        return pages.dropFirst(at.page + 1).first { !$0.blocks.isEmpty }?.blocks.first
    }

    /// The previous block in reading order, across pages.
    func block(before id: BlockID) -> EditorBlock? {
        guard let at = location(of: id) else { return nil }
        if at.block > 0 {
            return pages[at.page].blocks[at.block - 1]
        }
        return pages[..<at.page].last { !$0.blocks.isEmpty }?.blocks.last
    }

    // MARK: Editing

    mutating func setText(_ text: String, of id: BlockID) {
        guard let at = location(of: id), self[at].isText else { return }
        pages[at.page].blocks[at.block].content = .text(text)
    }

    mutating func updateDrawing(_ id: BlockID, source: String) {
        guard let at = location(of: id), var drawing = self[at].drawing else { return }
        drawing.source = source
        pages[at.page].blocks[at.block].content = .drawing(drawing)
    }

    /// Splits text block `id` at `offset` and puts a new drawing between the
    /// halves. Returns the drawing's block id.
    @discardableResult
    mutating func insertDrawing(source: String, splitting id: BlockID, at offset: Int) -> BlockID? {
        guard let at = location(of: id), let text = self[at].text else { return nil }
        let ns = text as NSString
        let offset = min(max(offset, 0), ns.length)
        let before = Self.trimmingNewlines(ns.substring(to: offset), leading: false)
        let after = Self.trimmingNewlines(ns.substring(from: offset), leading: true)
        let drawing = makeDrawingBlock(source: source)
        var replacement: [EditorBlock] = []
        if !before.isEmpty {
            replacement.append(EditorBlock(id: id, content: .text(before)))
        }
        replacement.append(drawing)
        replacement.append(EditorBlock(id: before.isEmpty ? id : makeID(), content: .text(after)))
        pages[at.page].blocks.replaceSubrange(at.block...at.block, with: replacement)
        normalize(page: at.page)
        return drawing.id
    }

    /// Adds a drawing at the end of a page, before its trailing empty text block.
    @discardableResult
    mutating func appendDrawing(source: String, toPage page: Int) -> BlockID {
        let page = min(max(page, 0), pages.count - 1)
        let drawing = makeDrawingBlock(source: source)
        var index = pages[page].blocks.count
        if let last = pages[page].blocks.last, last.text?.isEmpty == true {
            index -= 1
        }
        pages[page].blocks.insert(drawing, at: index)
        normalize(page: page)
        return drawing.id
    }

    /// Inserts an empty page after `page` and returns its text block.
    @discardableResult
    mutating func insertPage(after page: Int) -> BlockID {
        let index = min(max(page + 1, 0), pages.count)
        let block = EditorBlock(id: makeID(), content: .text(""))
        pages.insert(EditorPage(id: makePageID(), blocks: [block]), at: index)
        return block.id
    }

    /// Removes a page; the only page can't be removed.
    mutating func removePage(_ page: Int) {
        guard pages.count > 1, pages.indices.contains(page) else { return }
        for block in pages[page].blocks {
            if let drawing = block.drawing { retireDrawingID(drawing.id) }
        }
        pages.remove(at: page)
    }

    /// Joins page `page` onto the end of the page before it (removes the page break).
    mutating func mergePageWithPrevious(_ page: Int) {
        guard page > 0, pages.indices.contains(page) else { return }
        let blocks = pages.remove(at: page).blocks
        pages[page - 1].blocks += blocks
        normalize(page: page - 1)
    }

    /// Removes a block. Returns the block that should take focus instead.
    @discardableResult
    mutating func removeBlock(_ id: BlockID) -> BlockID? {
        guard let at = location(of: id) else { return nil }
        if let drawing = self[at].drawing { retireDrawingID(drawing.id) }
        pages[at.page].blocks.remove(at: at.block)
        normalize(page: at.page)
        let blocks = pages[at.page].blocks
        return blocks[max(0, min(at.block - 1, blocks.count - 1))].id
    }

    /// Inserts a copy after the block (drawings get a fresh id) and returns it.
    @discardableResult
    mutating func duplicateBlock(_ id: BlockID) -> BlockID? {
        guard let at = location(of: id) else { return nil }
        let copy: EditorBlock
        switch self[at].content {
        case .text(let text): copy = EditorBlock(id: makeID(), content: .text(text))
        case .drawing(let drawing): copy = makeDrawingBlock(source: drawing.source)
        }
        pages[at.page].blocks.insert(copy, at: at.block + 1)
        normalize(page: at.page)
        return copy.id
    }

    /// Moves a block one place up (`-1`) or down (`+1`), crossing into the
    /// neighbouring page at a page edge. Returns false if it can't move.
    @discardableResult
    mutating func moveBlock(_ id: BlockID, by delta: Int) -> Bool {
        guard let at = location(of: id), delta == -1 || delta == 1 else { return false }
        var blocks = pages[at.page].blocks
        let target = at.block + delta
        // A page's trailing empty text block isn't a real neighbour to swap with.
        let lastReal = blocks.last?.text?.isEmpty == true ? blocks.count - 2 : blocks.count - 1
        if target >= 0 && target <= lastReal {
            blocks.swapAt(at.block, target)
            pages[at.page].blocks = blocks
            normalize(page: at.page)
            return true
        }
        let page = at.page + delta
        guard pages.indices.contains(page) else { return false }
        let block = pages[at.page].blocks.remove(at: at.block)
        if delta < 0 {
            let end = pages[page].blocks.last?.text?.isEmpty == true
                ? pages[page].blocks.count - 1 : pages[page].blocks.count
            pages[page].blocks.insert(block, at: end)
        } else {
            pages[page].blocks.insert(block, at: 0)
        }
        normalize(page: at.page)
        normalize(page: page)
        return true
    }

    /// Joins text block `id` onto the text block before it on the same page.
    /// Returns the merged block and the offset where the joined text starts.
    mutating func mergeWithPrevious(_ id: BlockID) -> (block: BlockID, offset: Int)? {
        guard let at = location(of: id), at.block > 0, let text = self[at].text,
              let previous = pages[at.page].blocks[at.block - 1].text else { return nil }
        let previousID = pages[at.page].blocks[at.block - 1].id
        let joined = previous.isEmpty ? text : text.isEmpty ? previous : previous + "\n" + text
        let offset = previous.isEmpty ? 0 : (previous as NSString).length + (text.isEmpty ? 0 : 1)
        pages[at.page].blocks[at.block - 1].content = .text(joined)
        pages[at.page].blocks.remove(at: at.block)
        normalize(page: at.page)
        return (previousID, offset)
    }

    /// The text block right after drawing `id`, inserting an empty one if the
    /// next block isn't text.
    mutating func textBlock(after id: BlockID) -> BlockID? {
        guard let at = location(of: id) else { return nil }
        let next = at.block + 1
        if next < pages[at.page].blocks.count, pages[at.page].blocks[next].isText {
            return pages[at.page].blocks[next].id
        }
        let block = EditorBlock(id: makeID(), content: .text(""))
        pages[at.page].blocks.insert(block, at: next)
        return block.id
    }

    // MARK: External changes

    /// `note` with this document's block and page ids carried over where the
    /// blocks still correspond: drawings by drawing id, text blocks by page
    /// index and position among the page's text blocks, pages by index.
    /// A text block whose content only differs by surrounding blank lines
    /// keeps the editor's text, so the caret doesn't jump.
    func reconciled(with note: Note) -> EditorDocument {
        var result = EditorDocument(note: note, lastID: lastID)
        var drawingIDs: [String: BlockID] = [:]
        for page in pages {
            for block in page.blocks {
                if let drawing = block.drawing { drawingIDs[drawing.id] = block.id }
            }
        }
        var used = Set<BlockID>()
        for p in result.pages.indices {
            let oldText = pages.indices.contains(p) ? pages[p].blocks.filter(\.isText) : []
            if pages.indices.contains(p) {
                result.pages[p].id = pages[p].id
            }
            var textIndex = 0
            for b in result.pages[p].blocks.indices {
                let block = result.pages[p].blocks[b]
                var match: EditorBlock?
                switch block.content {
                case .drawing(let drawing):
                    match = drawingIDs[drawing.id].map { EditorBlock(id: $0, content: block.content) }
                case .text(let text):
                    if textIndex < oldText.count {
                        match = oldText[textIndex]
                        if let old = match?.text, Self.canonical(old) != Self.canonical(text) {
                            match?.content = block.content
                        }
                    }
                    textIndex += 1
                }
                if let match, !used.contains(match.id) {
                    used.insert(match.id)
                    result.pages[p].blocks[b] = match
                }
            }
        }
        return result
    }

    /// Takes `snapshot`'s content (for undo) without giving back drawing ids
    /// handed out since, so an id is never reused.
    func restoring(_ snapshot: EditorDocument) -> EditorDocument {
        var restored = snapshot
        restored.lastID = max(lastID, snapshot.lastID)
        let key = "last-drawing"
        let current = base.frontMatter[key].flatMap { Int($0) } ?? 0
        let old = snapshot.base.frontMatter[key].flatMap { Int($0) } ?? 0
        if current > old {
            restored.base.frontMatter[key] = String(current)
        }
        return restored
    }

    // MARK: Helpers

    private init(note: Note, lastID: Int) {
        self.lastID = lastID
        base = note
        base.pages = [NotePage()]
        pages = []
        for page in note.pages {
            var blocks: [EditorBlock] = []
            for block in page.blocks {
                blocks.append(makeBlock(block))
            }
            pages.append(EditorPage(id: makePageID(), blocks: blocks))
        }
        for p in pages.indices { normalize(page: p) }
    }

    /// Text as the file format stores it: without leading or trailing blank lines.
    static func canonical(_ text: String) -> String {
        guard let first = text.first, let last = text.last,
              first.isWhitespace || last.isWhitespace else { return text }
        var lines = text.split(separator: "\n", omittingEmptySubsequences: false)[...]
        while let line = lines.first, line.allSatisfy(\.isWhitespace) { lines = lines.dropFirst() }
        while let line = lines.last, line.allSatisfy(\.isWhitespace) { lines = lines.dropLast() }
        return lines.joined(separator: "\n")
    }

    private static func trimmingNewlines(_ text: String, leading: Bool) -> String {
        var text = Substring(text)
        if leading {
            while text.first == "\n" { text = text.dropFirst() }
        } else {
            while text.last == "\n" { text = text.dropLast() }
        }
        return String(text)
    }

    private mutating func makeID() -> BlockID {
        lastID += 1
        return BlockID(rawValue: lastID)
    }

    private mutating func makePageID() -> PageID {
        lastID += 1
        return PageID(rawValue: lastID)
    }

    private mutating func makeBlock(_ block: NoteBlock) -> EditorBlock {
        switch block {
        case .text(let text): return EditorBlock(id: makeID(), content: .text(text))
        case .drawing(let drawing): return EditorBlock(id: makeID(), content: .drawing(drawing))
        }
    }

    private mutating func makeDrawingBlock(source: String) -> EditorBlock {
        EditorBlock(id: makeID(), content: .drawing(Drawing(id: newDrawingID(), source: source)))
    }

    /// A drawing id that's unused and recorded as used, via `Note`'s own bookkeeping.
    private mutating func newDrawingID() -> String {
        var scratch = note
        let id = (try? scratch.insertDrawing("", page: 1)) ?? scratch.nextDrawingID()
        base.frontMatter = scratch.frontMatter
        return id
    }

    private mutating func retireDrawingID(_ id: String) {
        var scratch = note
        try? scratch.deleteDrawing(id)
        base.frontMatter = scratch.frontMatter
    }

    /// Drops empty text blocks next to other text, and makes the page end with text.
    private mutating func normalize(page p: Int) {
        var blocks: [EditorBlock] = []
        for block in pages[p].blocks {
            if let last = blocks.last, last.text?.isEmpty == true, block.isText {
                blocks[blocks.count - 1] = block
            } else if block.text?.isEmpty == true, blocks.last?.isText == true {
                continue
            } else {
                blocks.append(block)
            }
        }
        if blocks.last?.isText != true {
            blocks.append(EditorBlock(id: makeID(), content: .text("")))
        }
        pages[p].blocks = blocks
    }
}
