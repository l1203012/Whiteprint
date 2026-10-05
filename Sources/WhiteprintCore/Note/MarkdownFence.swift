import Foundation

/// A CommonMark fenced code block opener (```` ``` ```` or `~~~`).
///
/// The note parser has to track fences so a `+++page` line or a ```` ```wp ````
/// line inside someone's code block isn't mistaken for structure.
struct MarkdownFence: Equatable {
    let marker: Character
    let length: Int
    let info: String

    /// Parses `line` as an opening fence, or returns nil if it isn't one.
    static func opening(_ line: Substring) -> MarkdownFence? {
        guard let rest = stripIndent(line), let marker = rest.first, marker == "`" || marker == "~" else {
            return nil
        }
        let length = rest.prefix { $0 == marker }.count
        guard length >= 3 else { return nil }
        let info = rest.dropFirst(length).trimmingCharacters(in: .whitespaces)
        if marker == "`" && info.contains("`") { return nil }
        return MarkdownFence(marker: marker, length: length, info: info)
    }

    func isClosed(by line: Substring) -> Bool {
        guard let rest = Self.stripIndent(line) else { return false }
        let run = rest.prefix { $0 == marker }.count
        return run >= length && rest.dropFirst(run).allSatisfy { $0 == " " || $0 == "\t" }
    }

    var infoWords: [Substring] {
        info.split(whereSeparator: { $0 == " " || $0 == "\t" })
    }

    /// Reads `key=value` from the info string, e.g. `id` from ```` ```wp id=d1 ````.
    func attribute(_ key: String) -> String? {
        let prefix = key + "="
        return infoWords.dropFirst().first { $0.hasPrefix(prefix) }.map { String($0.dropFirst(prefix.count)) }
    }

    /// Returns the fence left open at the end of `text`, if any.
    static func unclosed(in text: String) -> MarkdownFence? {
        var open: MarkdownFence?
        for line in text.split(separator: "\n", omittingEmptySubsequences: false) {
            if let fence = open {
                if fence.isClosed(by: line) { open = nil }
            } else {
                open = opening(line)
            }
        }
        return open
    }

    /// Removes up to three leading spaces; four or more means an indented code line.
    private static func stripIndent(_ line: Substring) -> Substring? {
        let spaces = line.prefix { $0 == " " }.count
        return spaces <= 3 ? line.dropFirst(spaces) : nil
    }
}
