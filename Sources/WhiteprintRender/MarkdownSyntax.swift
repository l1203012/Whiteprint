import Foundation

/// An open fenced code block: the fence character and how long its run was.
struct MarkdownFenceState: Equatable {
    var marker: UInt16
    var length: Int

    /// A compact string form, stored as a text attribute between restyles.
    var encoded: String { "\(Character(Unicode.Scalar(marker)!))\(length)" }

    init(marker: UInt16, length: Int) {
        self.marker = marker
        self.length = length
    }

    init?(encoded: String) {
        guard let first = encoded.utf16.first, let length = Int(encoded.dropFirst()) else { return nil }
        self.init(marker: first, length: length)
    }
}

enum MarkdownLineKind: Equatable {
    case blank
    case paragraph
    case heading(level: Int)
    case bullet
    case ordered
    case task(checked: Bool)
    case quote
    case divider
    /// The opening or closing line of a fenced code block.
    case fence
    /// A line inside a fenced code block.
    case code
}

/// How one line of Markdown is styled as a block.
struct MarkdownLine: Equatable {
    var kind: MarkdownLineKind
    /// UTF-16 length of the leading markup (indent, marker and the space after
    /// it), e.g. `## `, `  - [ ] `, `> `. Inline parsing starts after it.
    var markerLength: Int = 0
}

/// An inline element. `range` covers the markup, `contentRange` the text
/// between it; both are UTF-16 ranges in the line.
struct MarkdownSpan: Equatable {
    enum Kind: Equatable {
        case bold, italic, code, link
    }

    var kind: Kind
    var range: NSRange
    var contentRange: NSRange
    /// The destination of a link.
    var url: String?

    /// The parts of `range` outside `contentRange`.
    var markupRanges: [NSRange] {
        [
            NSRange(location: range.location, length: contentRange.location - range.location),
            NSRange(location: NSMaxRange(contentRange), length: NSMaxRange(range) - NSMaxRange(contentRange)),
        ].filter { $0.length > 0 }
    }
}

/// Line and inline Markdown recognition for the styler. Works on UTF-16 so
/// ranges map straight onto `NSAttributedString`.
enum MarkdownSyntax {
    /// Classifies `line` (without its line break). `openFence` is the fenced
    /// code block the line starts in; the result says which one it leaves open.
    static func classify(_ line: String, openFence: MarkdownFenceState?) -> (line: MarkdownLine, openFence: MarkdownFenceState?) {
        let c = Array(line.utf16)
        let indent = c.prefix { $0 == space || $0 == tab }.count
        if let fence = openFence {
            let length = indent <= 3 ? run(of: fence.marker, in: c, at: indent) : 0
            let closes = length >= fence.length && c[(indent + length)...].allSatisfy(isWhitespace)
            return closes ? (MarkdownLine(kind: .fence), nil) : (MarkdownLine(kind: .code), fence)
        }
        if indent == c.count {
            return (MarkdownLine(kind: .blank), nil)
        }
        let first = c[indent]
        if indent <= 3, first == backtick || first == tilde {
            let length = run(of: first, in: c, at: indent)
            if length >= 3 && !(first == backtick && c[(indent + length)...].contains(backtick)) {
                return (MarkdownLine(kind: .fence), MarkdownFenceState(marker: first, length: length))
            }
        }
        return (classifyBlock(c, indent: indent), nil)
    }

    private static func classifyBlock(_ c: [UInt16], indent: Int) -> MarkdownLine {
        let first = c[indent]
        if indent <= 3, first == hash {
            let level = run(of: hash, in: c, at: indent)
            let end = indent + level
            if level <= 6, end == c.count || isWhitespace(c[end]) {
                return MarkdownLine(kind: .heading(level: level), markerLength: end + spaces(in: c, at: end))
            }
        }
        if indent <= 3, first == dash || first == star || first == underscore {
            let rest = c[indent...]
            if rest.filter({ $0 == first }).count >= 3, rest.allSatisfy({ $0 == first || isWhitespace($0) }) {
                return MarkdownLine(kind: .divider, markerLength: c.count)
            }
        }
        if first == dash || first == star || first == plus, indent + 1 < c.count, isWhitespace(c[indent + 1]) {
            let afterMarker = indent + 1 + spaces(in: c, at: indent + 1)
            if afterMarker + 2 < c.count, c[afterMarker] == openBracket, c[afterMarker + 2] == closeBracket,
               afterMarker + 3 == c.count || isWhitespace(c[afterMarker + 3]) {
                let mark = c[afterMarker + 1]
                if mark == space || mark == lowerX || mark == upperX {
                    let end = afterMarker + 3
                    return MarkdownLine(kind: .task(checked: mark != space), markerLength: end + spaces(in: c, at: end))
                }
            }
            return MarkdownLine(kind: .bullet, markerLength: afterMarker)
        }
        let digits = c[indent...].prefix(while: isDigit).count
        if (1...9).contains(digits), indent + digits + 1 < c.count {
            let delimiter = c[indent + digits]
            if delimiter == dot || delimiter == closeParen, isWhitespace(c[indent + digits + 1]) {
                let end = indent + digits + 1
                return MarkdownLine(kind: .ordered, markerLength: end + spaces(in: c, at: end))
            }
        }
        if indent <= 3, first == greater {
            let length = c[indent...].prefix { $0 == greater || isWhitespace($0) }.count
            return MarkdownLine(kind: .quote, markerLength: indent + length)
        }
        return MarkdownLine(kind: .paragraph)
    }

    /// Inline code, links, bold and italic in `text`, with ranges offset by `offset`.
    static func spans(in text: String, offset: Int = 0) -> [MarkdownSpan] {
        var scanner = InlineScanner(Array(text.utf16))
        return scanner.scan().map { span in
            var span = span
            span.range.location += offset
            span.contentRange.location += offset
            return span
        }
    }

    // MARK: Characters

    static let space: UInt16 = 0x20, tab: UInt16 = 0x09
    static let backtick: UInt16 = 0x60, tilde: UInt16 = 0x7E, hash: UInt16 = 0x23
    static let dash: UInt16 = 0x2D, star: UInt16 = 0x2A, plus: UInt16 = 0x2B, underscore: UInt16 = 0x5F
    static let openBracket: UInt16 = 0x5B, closeBracket: UInt16 = 0x5D
    static let openParen: UInt16 = 0x28, closeParen: UInt16 = 0x29
    static let lowerX: UInt16 = 0x78, upperX: UInt16 = 0x58
    static let dot: UInt16 = 0x2E, greater: UInt16 = 0x3E, backslash: UInt16 = 0x5C

    static func isWhitespace(_ c: UInt16) -> Bool { c == space || c == tab }
    static func isDigit(_ c: UInt16) -> Bool { (0x30...0x39).contains(c) }

    /// Letters, digits and anything non-ASCII, for the intraword `_` rule.
    static func isWordCharacter(_ c: UInt16) -> Bool {
        isDigit(c) || (0x41...0x5A).contains(c) || (0x61...0x7A).contains(c) || c > 0x7F
    }

    static func isASCIIPunctuation(_ c: UInt16) -> Bool {
        (0x21...0x2F).contains(c) || (0x3A...0x40).contains(c) || (0x5B...0x60).contains(c) || (0x7B...0x7E).contains(c)
    }

    private static func run(of marker: UInt16, in c: [UInt16], at index: Int) -> Int {
        c[index...].prefix { $0 == marker }.count
    }

    private static func spaces(in c: [UInt16], at index: Int) -> Int {
        c[index...].prefix(while: isWhitespace).count
    }
}

/// Finds inline spans in one line: code spans first (nothing inside them is
/// markup), then links, then `**bold**` / `__bold__`, then `*italic*` / `_italic_`.
private struct InlineScanner {
    private typealias S = MarkdownSyntax
    private let c: [UInt16]
    /// Characters that can't be delimiters: escaped, inside code, or already used.
    private var taken: [Bool]
    private var spans: [MarkdownSpan] = []

    init(_ c: [UInt16]) {
        self.c = c
        taken = Array(repeating: false, count: c.count)
    }

    mutating func scan() -> [MarkdownSpan] {
        markEscapes()
        scanCode()
        scanLinks()
        scanEmphasis(width: 2, kind: .bold)
        scanEmphasis(width: 1, kind: .italic)
        return spans.sorted { $0.range.location < $1.range.location }
    }

    private mutating func markEscapes() {
        var i = 0
        while i + 1 < c.count {
            if c[i] == S.backslash, S.isASCIIPunctuation(c[i + 1]) {
                taken[i + 1] = true
                i += 2
            } else {
                i += 1
            }
        }
    }

    private mutating func scanCode() {
        var i = 0
        while i < c.count {
            guard c[i] == S.backtick, !taken[i] else { i += 1; continue }
            let length = c[i...].prefix { $0 == S.backtick }.count
            var j = i + length
            var close: Int?
            while j < c.count {
                if c[j] == S.backtick {
                    let run = c[j...].prefix { $0 == S.backtick }.count
                    if run == length { close = j; break }
                    j += run
                } else {
                    j += 1
                }
            }
            guard let end = close, end > i + length else { i += length; continue }
            add(.code, from: i, to: end + length, content: NSRange(location: i + length, length: end - i - length))
            i = end + length
        }
    }

    private mutating func scanLinks() {
        var i = 0
        while i < c.count {
            guard c[i] == S.openBracket, !taken[i],
                  let close = firstFree(S.closeBracket, from: i + 1),
                  close + 1 < c.count, c[close + 1] == S.openParen,
                  let end = firstFree(S.closeParen, from: close + 2)
            else { i += 1; continue }
            let url = String(utf16CodeUnits: Array(c[(close + 2)..<end]), count: end - close - 2)
            add(.link, from: i, to: end + 1, content: NSRange(location: i + 1, length: close - i - 1), url: url)
            taken[i] = true
            for k in close...end { taken[k] = true }
            i = end + 1
        }
    }

    private mutating func scanEmphasis(width: Int, kind: MarkdownSpan.Kind) {
        var i = 0
        while i + width < c.count {
            guard let marker = delimiter(at: i, width: width), canOpen(at: i, width: width, marker: marker),
                  let close = closer(for: marker, width: width, from: i + width + 1)
            else { i += 1; continue }
            add(kind, from: i, to: close + width, content: NSRange(location: i + width, length: close - i - width))
            for k in 0..<width {
                taken[i + k] = true
                taken[close + k] = true
            }
            i = close + width
        }
    }

    /// The `*` or `_` starting a free run of exactly `width` characters at `i`.
    private func delimiter(at i: Int, width: Int) -> UInt16? {
        let marker = c[i]
        guard marker == S.star || marker == S.underscore, i + width <= c.count else { return nil }
        for k in 0..<width where c[i + k] != marker || taken[i + k] { return nil }
        return marker
    }

    private func canOpen(at i: Int, width: Int, marker: UInt16) -> Bool {
        let next = i + width
        guard next < c.count, !S.isWhitespace(c[next]) else { return false }
        return marker != S.underscore || i == 0 || !S.isWordCharacter(c[i - 1])
    }

    private func closer(for marker: UInt16, width: Int, from start: Int) -> Int? {
        var j = start
        while j + width <= c.count {
            if delimiter(at: j, width: width) == marker, !S.isWhitespace(c[j - 1]),
               marker != S.underscore || j + width == c.count || !S.isWordCharacter(c[j + width]) {
                return j
            }
            j += 1
        }
        return nil
    }

    private func firstFree(_ character: UInt16, from start: Int) -> Int? {
        var j = start
        while j < c.count {
            if c[j] == character && !taken[j] { return j }
            j += 1
        }
        return nil
    }

    private mutating func add(_ kind: MarkdownSpan.Kind, from start: Int, to end: Int, content: NSRange, url: String? = nil) {
        spans.append(MarkdownSpan(
            kind: kind, range: NSRange(location: start, length: end - start), contentRange: content, url: url
        ))
        if kind == .code {
            for k in start..<end { taken[k] = true }
        }
    }
}
