import Foundation

/// The block-level prefix of one Markdown line: indentation plus a heading,
/// list, checklist or quote marker. Lengths are UTF-16 (the prefix is ASCII).
struct MarkdownLine: Equatable {
    enum Kind: Equatable {
        case paragraph
        case heading(Int)
        case bullet(Character)
        case ordered(Int, delimiter: Character)
        case checklist(checked: Bool, bullet: Character)
        case quote
    }

    var kind: Kind
    /// Leading spaces and tabs.
    var indent: String
    /// Indent plus marker: where the line's content starts.
    var prefixLength: Int

    init<S: StringProtocol>(_ line: S) {
        let chars = Array(line)
        var i = 0
        while i < chars.count, chars[i] == " " || chars[i] == "\t" { i += 1 }
        indent = String(chars[..<i])
        kind = .paragraph
        prefixLength = i

        func space(at j: Int) -> Bool { j < chars.count && chars[j] == " " }

        if i < chars.count, chars[i] == "#" {
            var j = i
            while j < chars.count, chars[j] == "#" { j += 1 }
            let level = j - i
            if level <= 6, space(at: j) {
                kind = .heading(level)
                prefixLength = j + 1
            }
        } else if i < chars.count, "-*+".contains(chars[i]), space(at: i + 1) {
            let bullet = chars[i]
            if i + 4 < chars.count, chars[i + 2] == "[", chars[i + 4] == "]", " xX".contains(chars[i + 3]),
               i + 5 == chars.count || space(at: i + 5) {
                kind = .checklist(checked: chars[i + 3] != " ", bullet: bullet)
                prefixLength = min(i + 6, chars.count)
            } else {
                kind = .bullet(bullet)
                prefixLength = i + 2
            }
        } else if i < chars.count, chars[i].isASCII, chars[i].isNumber {
            var j = i
            while j < chars.count, j - i < 9, chars[j].isASCII, chars[j].isNumber { j += 1 }
            if j < chars.count, chars[j] == "." || chars[j] == ")", space(at: j + 1),
               let number = Int(String(chars[i..<j])) {
                kind = .ordered(number, delimiter: chars[j])
                prefixLength = j + 2
            }
        } else if i < chars.count, chars[i] == ">" {
            kind = .quote
            prefixLength = space(at: i + 1) ? i + 2 : i + 1
        }
    }

    var isListItem: Bool {
        switch kind {
        case .bullet, .ordered, .checklist: return true
        case .paragraph, .heading, .quote: return false
        }
    }

    /// The marker for the item after this one, e.g. `2. ` after `1. `.
    var continuationMarker: String? {
        switch kind {
        case .bullet(let bullet): return "\(bullet) "
        case .ordered(let number, let delimiter): return "\(number + 1)\(delimiter) "
        case .checklist(_, let bullet): return "\(bullet) [ ] "
        case .paragraph, .heading, .quote: return nil
        }
    }

    /// The UTF-16 range of `[ ]` / `[x]` within the line, for checklist lines.
    var checkboxRange: NSRange? {
        guard case .checklist = kind else { return nil }
        return NSRange(location: (indent as NSString).length + 2, length: 3)
    }
}

/// One line of a text block, without its line terminator.
struct TextLine: Equatable {
    var range: NSRange
    var text: String

    var start: Int { range.location }
    var end: Int { NSMaxRange(range) }

    /// The line containing UTF-16 offset `location`.
    static func at(_ location: Int, in text: NSString) -> TextLine {
        let location = min(max(location, 0), text.length)
        var start = 0, end = 0, contentsEnd = 0
        text.getLineStart(&start, end: &end, contentsEnd: &contentsEnd, for: NSRange(location: location, length: 0))
        // At the very end, after a final newline, the caret is on an empty last line.
        if location == text.length, location > 0, contentsEnd < location {
            return TextLine(range: NSRange(location: location, length: 0), text: "")
        }
        let range = NSRange(location: start, length: contentsEnd - start)
        return TextLine(range: range, text: text.substring(with: range))
    }

    /// Every line in `text`, including an empty last line after a final newline.
    static func all(in text: NSString) -> [TextLine] {
        var lines: [TextLine] = []
        var location = 0
        while true {
            let line = at(location, in: text)
            lines.append(line)
            if line.end >= text.length { break }
            location = line.end + 1
        }
        if text.length > 0, text.character(at: text.length - 1) == 0x0A, lines.last?.end != text.length {
            lines.append(TextLine(range: NSRange(location: text.length, length: 0), text: ""))
        }
        return lines
    }
}

/// Fenced code blocks (```` ``` ```` / `~~~`) are where Markdown shortcuts stop applying.
enum CodeFence {
    struct Opener: Equatable {
        var marker: Character
        var length: Int
    }

    static func opener(_ line: String) -> Opener? {
        let rest = line.drop { $0 == " " }
        guard line.count - rest.count <= 3, let marker = rest.first, marker == "`" || marker == "~" else { return nil }
        let length = rest.prefix { $0 == marker }.count
        guard length >= 3 else { return nil }
        if marker == "`", rest.dropFirst(length).contains("`") { return nil }
        return Opener(marker: marker, length: length)
    }

    static func closes(_ opener: Opener, _ line: String) -> Bool {
        let rest = line.drop { $0 == " " }
        guard line.count - rest.count <= 3 else { return false }
        let run = rest.prefix { $0 == opener.marker }.count
        return run >= opener.length && rest.dropFirst(run).allSatisfy { $0 == " " || $0 == "\t" }
    }

    /// The fence open at the start of the line beginning at `lineStart`, if any.
    static func open(before lineStart: Int, in text: NSString) -> Opener? {
        var open: Opener?
        for line in TextLine.all(in: text) where line.start < lineStart {
            if let fence = open {
                if closes(fence, line.text) { open = nil }
            } else {
                open = opener(line.text)
            }
        }
        return open
    }

    /// Whether the line at `lineStart` is code (inside a fence, or a fence line itself).
    static func isCode(lineAt lineStart: Int, in text: NSString) -> Bool {
        open(before: lineStart, in: text) != nil
    }

    /// Whether a fence opened on the line at `lineStart` is left unclosed.
    static func isUnclosed(openingAt lineStart: Int, in text: NSString) -> Bool {
        let line = TextLine.at(lineStart, in: text)
        guard let fence = opener(line.text) else { return false }
        return !TextLine.all(in: text).contains { $0.start > line.start && closes(fence, $0.text) }
    }
}
