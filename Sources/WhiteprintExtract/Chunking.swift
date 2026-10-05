import Foundation

/// A `[ref]` block: one unit, or one piece of a unit too big for a chunk.
struct ChunkBlock {
    let ref: String
    let text: String
    let length: Int

    init(ref: String, body: String) {
        self.ref = ref
        self.text = "[\(ref)]\n\(body)"
        self.length = text.count
    }

    /// Splits `unit` into blocks that each fit in `limit` characters, breaking
    /// at paragraphs, then lines, then spaces, and only mid-word as a last resort.
    static func blocks(for unit: ExtractedUnit, limit: Int) -> [ChunkBlock] {
        let whole = ChunkBlock(ref: unit.ref, body: unit.text)
        if whole.length <= limit { return [whole] }
        let budget = max(1, limit - "[\(unit.ref)]\n".count)
        return pack(unit.text, budget: budget, separators: ["\n\n", "\n", " "])
            .map { ChunkBlock(ref: unit.ref, body: $0) }
    }

    private static func pack(_ text: String, budget: Int, separators: [String]) -> [String] {
        if text.count <= budget { return [text] }
        guard let separator = separators.first else {
            return stride(from: 0, to: text.count, by: budget).map { start in
                let from = text.index(text.startIndex, offsetBy: start)
                let to = text.index(from, offsetBy: budget, limitedBy: text.endIndex) ?? text.endIndex
                return String(text[from..<to])
            }
        }
        let rest = Array(separators.dropFirst())
        var pieces: [String] = []
        var current = ""
        var length = 0
        for part in text.components(separatedBy: separator) where !part.isEmpty {
            for piece in pack(part, budget: budget, separators: rest) {
                let count = piece.count
                if length > 0 && length + separator.count + count <= budget {
                    current += separator + piece
                    length += separator.count + count
                } else {
                    if length > 0 { pieces.append(current) }
                    current = piece
                    length = count
                }
            }
        }
        if length > 0 { pieces.append(current) }
        return pieces
    }
}

extension Chunker {
    static func makeChunks(_ document: ExtractedDocument, maxCharacters: Int) -> [ExtractedChunk] {
        let blocks = document.units.flatMap { ChunkBlock.blocks(for: $0, limit: maxCharacters) }
        var chunks: [ExtractedChunk] = []
        var group: [ChunkBlock] = []
        var length = 0

        func flush() {
            guard let first = group.first, let last = group.last else { return }
            chunks.append(ExtractedChunk(
                index: chunks.count + 1,
                firstRef: first.ref,
                lastRef: last.ref,
                text: group.map(\.text).joined(separator: "\n\n")
            ))
            group = []
            length = 0
        }

        for block in blocks {
            let added = group.isEmpty ? block.length : length + 2 + block.length
            if !group.isEmpty && added > maxCharacters { flush() }
            length = group.isEmpty ? block.length : length + 2 + block.length
            group.append(block)
        }
        flush()
        return chunks
    }
}
