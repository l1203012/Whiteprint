import Foundation

/// Normalizes raw extracted text so Claude reads content, not layout noise.
enum TextCleaner {
    /// Cleans every unit, removes repeated headers and footers, and drops
    /// units left empty.
    static func clean(_ units: [ExtractedUnit]) -> [ExtractedUnit] {
        var lines = units.map { cleanLines($0.text) }
        removeRepeatedEdges(&lines)
        return zip(units, lines).compactMap { unit, lines in
            let text = joinParagraphs(lines)
            return text.isEmpty ? nil : ExtractedUnit(ref: unit.ref, text: text)
        }
    }

    /// Cleans a single text: whitespace, hyphenation and page-number lines.
    /// Empty strings in the result mark paragraph breaks.
    static func cleanLines(_ text: String) -> [String] {
        let normalized = text
            .replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\u{00AD}", with: "")
        var lines: [String] = []
        for raw in normalized.split(omittingEmptySubsequences: false, whereSeparator: \.isNewline) {
            let line = collapseWhitespace(raw)
            if isPageNumber(line) { continue }
            if let last = lines.last, endsWithBrokenWord(last), line.first?.isLowercase == true {
                lines[lines.count - 1] = String(last.dropLast()) + line
            } else {
                lines.append(line)
            }
        }
        return lines
    }

    static func joinParagraphs(_ lines: [String]) -> String {
        var result: [String] = []
        for line in lines {
            if line.isEmpty, result.last?.isEmpty ?? true { continue }
            result.append(line)
        }
        while result.last?.isEmpty == true { result.removeLast() }
        return result.joined(separator: "\n")
    }

    static func collapseWhitespace<S: StringProtocol>(_ text: S) -> String {
        text.split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
    }

    private static let pageNumberPattern = try! NSRegularExpression(
        pattern: #"^[-–—|\s]*(?:(?:page|pagina|blz\.?|p\.|pg\.?|seite|s\.)\s*)?\d{1,4}(?:\s*(?:/|of|van|de|sur|von)\s*\d{1,4})?[-–—|\s]*$"#,
        options: [.caseInsensitive]
    )

    /// `12`, `- 12 -`, `Page 12`, `12 / 40`, `p. 3 of 9`, `Pagina 4 van 10`.
    static func isPageNumber(_ line: String) -> Bool {
        guard !line.isEmpty, line.count <= 24 else { return false }
        let range = NSRange(line.startIndex..., in: line)
        return pageNumberPattern.firstMatch(in: line, range: range) != nil
    }

    private static func endsWithBrokenWord(_ line: String) -> Bool {
        guard line.hasSuffix("-"), line.count >= 2 else { return false }
        return line.dropLast().last?.isLetter == true
    }

    /// Lines this close to the start or end of a unit count as header or footer.
    private static let edgeDepth = 2
    /// Running headers are short; longer lines are only removed when repeated exactly.
    private static let maxMaskedLength = 80

    /// Removes lines that open or close more than half of the units (with at
    /// least three units): running headers, course titles, footers. For a
    /// short first or last line digits are ignored, so `Chapter 2 · 14`
    /// matches `Chapter 2 · 15`.
    static func removeRepeatedEdges(_ units: inout [[String]]) {
        guard units.count >= 3 else { return }
        var counts: [String: Int] = [:]
        for lines in units {
            Set(edgeKeys(lines).flatMap(\.keys)).forEach { counts[$0, default: 0] += 1 }
        }
        let repeated = Set(counts.filter { $0.value * 2 > units.count }.keys)
        guard !repeated.isEmpty else { return }
        for i in units.indices {
            let edges = edgeKeys(units[i])
            var drop = Set(edges.filter { !repeated.isDisjoint(with: $0.keys) }.map(\.index))
            // Digit-insensitive matches never empty a unit: `Slide 3` alone is content.
            if drop.count == units[i].filter({ !$0.isEmpty }).count {
                drop = Set(edges.filter { repeated.contains($0.keys[0]) }.map(\.index))
            }
            units[i] = units[i].enumerated().filter { !drop.contains($0.offset) }.map(\.element)
        }
    }

    private static func edgeKeys(_ lines: [String]) -> [(index: Int, keys: [String])] {
        let content = lines.indices.filter { !lines[$0].isEmpty }
        let outer = Set([content.first, content.last].compactMap { $0 })
        return Set(content.prefix(edgeDepth) + content.suffix(edgeDepth)).sorted().map { index in
            let exact = lines[index].lowercased()
            guard outer.contains(index), exact.count <= maxMaskedLength else { return (index, [exact]) }
            return (index, [exact, "#" + String(exact.map { $0.isNumber ? "#" : $0 })])
        }
    }
}
