/// A link between two ids in a chain like `a>b<>c-d`.
struct DSLLink: Equatable {
    var startArrow: Bool
    var endArrow: Bool

    static let forward = DSLLink(startArrow: false, endArrow: true)
}

/// `a>b>c`: ids joined by links. `links.count == ids.count - 1`.
struct DSLChain: Equatable {
    var ids: [String]
    var links: [DSLLink]
}

enum DSLToken: Equatable {
    case word(String)
    case string(String)
    case point(GridPoint)
    case size(GridSize)
    case number(Double)
    case chain(DSLChain)
}

struct DSLSyntaxError: Error, Equatable {
    var message: String
}

enum DSLTokenizer {
    /// Largest coordinate or size accepted, in grid units. Keeps a typo like
    /// `box a 99999,0` from asking the renderer for a huge canvas.
    static let maxMagnitude = 1000.0

    static func tokenize(_ line: Substring) throws -> [DSLToken] {
        var tokens: [DSLToken] = []
        var i = line.startIndex
        while i < line.endIndex {
            let c = line[i]
            if c.isWhitespace {
                i = line.index(after: i)
            } else if c == "#" {
                break
            } else if c == "\"" {
                let (string, end) = try readString(line, from: i)
                tokens.append(.string(string))
                i = end
            } else {
                var end = i
                while end < line.endIndex, !line[end].isWhitespace, line[end] != "#", line[end] != "\"" {
                    end = line.index(after: end)
                }
                tokens.append(try classify(String(line[i..<end])))
                i = end
            }
        }
        return tokens
    }

    /// Reads a quoted string starting at `start`. Supports `\"`, `\\` and `\n`.
    private static func readString(_ line: Substring, from start: Substring.Index) throws -> (String, Substring.Index) {
        var result = ""
        var i = line.index(after: start)
        while i < line.endIndex {
            let c = line[i]
            i = line.index(after: i)
            if c == "\"" {
                return (result, i)
            }
            if c == "\\", i < line.endIndex {
                let escaped = line[i]
                i = line.index(after: i)
                result.append(escaped == "n" ? "\n" : escaped)
            } else {
                result.append(c)
            }
        }
        throw DSLSyntaxError(message: "unterminated \"label\"")
    }

    private static func classify(_ raw: String) throws -> DSLToken {
        if let number = try number(raw) {
            return .number(number)
        }
        if raw.contains(",") {
            let parts = raw.split(separator: ",", omittingEmptySubsequences: false)
            guard parts.count == 2, let x = try number(parts[0]), let y = try number(parts[1]) else {
                throw DSLSyntaxError(message: "bad position '\(raw)', use X,Y")
            }
            return .point(GridPoint(x, y))
        }
        if let first = raw.first, first.isNumber || first == "." {
            let parts = raw.split(separator: "x", omittingEmptySubsequences: false)
            guard parts.count == 2, let width = try number(parts[0]), let height = try number(parts[1]) else {
                throw DSLSyntaxError(message: "bad size '\(raw)', use WxH")
            }
            guard width > 0, height > 0 else {
                throw DSLSyntaxError(message: "size '\(raw)' must be positive")
            }
            return .size(GridSize(width, height))
        }
        if raw.contains(where: isLinkCharacter) {
            return .chain(try chain(raw))
        }
        guard isIdentifier(raw) else {
            throw DSLSyntaxError(message: "unexpected '\(raw)'")
        }
        return .word(raw)
    }

    /// Plain decimal numbers only, so `inf`, `nan` and `1e5` are not numbers.
    private static func number<S: StringProtocol>(_ raw: S) throws -> Double? {
        let digits = raw.hasPrefix("-") ? raw.dropFirst() : raw[...]
        guard !digits.isEmpty,
              digits.allSatisfy({ $0.isASCII && ($0.isNumber || $0 == ".") }),
              digits.filter({ $0 == "." }).count <= 1,
              digits != ".",
              let value = Double(String(raw)) else { return nil }
        guard abs(value) <= maxMagnitude else {
            throw DSLSyntaxError(message: "'\(raw)' is too large, max \(Int(maxMagnitude))")
        }
        return value
    }

    /// Parses `a>b`, `a->b`, `a<>b`, `a<-b`, `a-b`, chained any number of times.
    private static func chain(_ raw: String) throws -> DSLChain {
        let invalid = DSLSyntaxError(message: "bad link '\(raw)', use a>b")
        var ids: [String] = []
        var links: [DSLLink] = []
        var rest = raw[...]
        while true {
            let id = rest.prefix { !isLinkCharacter($0) }
            guard isIdentifier(id) else { throw invalid }
            ids.append(String(id))
            rest = rest.dropFirst(id.count)
            if rest.isEmpty { break }

            let op = rest.prefix(while: isLinkCharacter)
            rest = rest.dropFirst(op.count)
            let startArrow = op.hasPrefix("<")
            let endArrow = op.hasSuffix(">")
            let middle = op.dropFirst(startArrow ? 1 : 0).dropLast(endArrow ? 1 : 0)
            guard middle.allSatisfy({ $0 == "-" }) else { throw invalid }
            links.append(DSLLink(startArrow: startArrow, endArrow: endArrow))
        }
        guard ids.count >= 2 else { throw invalid }
        return DSLChain(ids: ids, links: links)
    }

    private static func isLinkCharacter(_ c: Character) -> Bool {
        c == "<" || c == ">" || c == "-"
    }

    /// Letters (any script), digits and `_`, not starting with a digit.
    static func isIdentifier<S: StringProtocol>(_ raw: S) -> Bool {
        guard let first = raw.first, first.isLetter || first == "_" else { return false }
        return raw.allSatisfy { $0.isLetter || $0.isNumber || $0 == "_" }
    }
}
