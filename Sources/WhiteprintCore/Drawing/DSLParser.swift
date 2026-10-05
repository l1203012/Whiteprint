enum DSLLayout: Equatable {
    case row, col, flow
}

enum DSLStatement: Equatable {
    case shape(ShapeKind, id: String?, at: GridPoint?, size: GridSize?, label: String?, style: DrawingStyle)
    /// `arrow a>b>c`: connectors between shapes or groups.
    case connect(DSLChain, label: String?, style: DrawingStyle)
    /// `line 0,0 10,0 10,5` or `path … closed`: free-standing points.
    case polyline([GridPoint], arrow: Bool, closed: Bool, label: String?, style: DrawingStyle)
    case dimension(GridPoint, GridPoint, label: String?)
    case group(id: String, members: [String], label: String?, style: DrawingStyle)
    /// For `row` and `col` only the chain's ids matter; `flow` also draws its links.
    case layout(DSLLayout, DSLChain, gap: Double?)
}

struct DSLParsedLine: Equatable {
    var line: Int
    var statement: DSLStatement
}

enum DSLParser {
    /// Drawings are meant to be small; this caps the work a single block can cause.
    static let maxStatements = 500

    private static let commands: [String: String] = [
        "box": "box", "rect": "box", "circle": "circle", "oval": "circle", "db": "db", "text": "text",
        "arrow": "arrow", "line": "line", "path": "path", "dim": "dim", "group": "group",
        "row": "row", "col": "col", "column": "col", "flow": "flow",
    ]

    static func parse(_ source: String) -> (lines: [DSLParsedLine], errors: [DrawingError]) {
        var lines: [DSLParsedLine] = []
        var errors: [DrawingError] = []
        for (offset, text) in source.split(separator: "\n", omittingEmptySubsequences: false).enumerated() {
            let number = offset + 1
            do {
                let tokens = try DSLTokenizer.tokenize(text)
                guard !tokens.isEmpty else { continue }
                guard lines.count < maxStatements else {
                    errors.append(DrawingError(line: number, message: "too many lines, max \(maxStatements)"))
                    break
                }
                lines.append(DSLParsedLine(line: number, statement: try statement(from: tokens)))
            } catch let error as DSLSyntaxError {
                errors.append(DrawingError(line: number, message: error.message))
            } catch {
                errors.append(DrawingError(line: number, message: "\(error)"))
            }
        }
        return (lines, errors)
    }

    static func statement(from tokens: [DSLToken]) throws -> DSLStatement {
        guard case .word(let word) = tokens[0] else {
            throw DSLSyntaxError(message: "expected a command like box, arrow or flow")
        }
        guard let command = commands[word.lowercased()] else {
            throw DSLSyntaxError(message: "unknown command '\(word)'")
        }
        let args = try Arguments(tokens.dropFirst())

        switch command {
        case "box", "circle", "db", "text":
            return try shape(ShapeKind(rawValue: command)!, args)
        case "arrow", "line":
            return try connector(arrow: command == "arrow", args)
        case "path":
            try args.reject(chains: true, sizes: true, numbers: true, words: true, command: command)
            guard args.points.count >= 2 else {
                throw DSLSyntaxError(message: "path needs at least 2 points, e.g. path 0,0 4,0 4,4 closed")
            }
            return .polyline(args.points, arrow: false, closed: args.closed, label: try args.label(), style: args.style)
        case "dim":
            try args.reject(chains: true, sizes: true, numbers: true, words: true, command: command)
            try args.rejectClosed(command)
            guard args.points.count == 2 else {
                throw DSLSyntaxError(message: "dim needs 2 points, e.g. dim 0,0 10,0")
            }
            return .dimension(args.points[0], args.points[1], label: try args.label())
        case "group":
            try args.reject(chains: true, points: true, sizes: true, numbers: true, command: command)
            try args.rejectClosed(command)
            guard args.words.count >= 2 else {
                throw DSLSyntaxError(message: "group needs an id and members, e.g. group g a b")
            }
            return .group(id: args.words[0], members: Array(args.words.dropFirst()), label: try args.label(), style: args.style)
        default:
            return try layout(command == "row" ? .row : command == "col" ? .col : .flow, args)
        }
    }

    private static func shape(_ kind: ShapeKind, _ args: Arguments) throws -> DSLStatement {
        let command = kind.rawValue
        try args.reject(chains: true, command: command)
        try args.rejectClosed(command)
        if args.words.count > 1 {
            throw DSLSyntaxError(message: "unexpected '\(args.words[1])'")
        }
        let id = args.words.first
        let label = try args.label()
        if kind == .text {
            guard label != nil else { throw DSLSyntaxError(message: "text needs a \"label\"") }
            guard args.sizes.isEmpty, args.numbers.isEmpty else {
                throw DSLSyntaxError(message: "text has no size")
            }
        } else if id == nil {
            throw DSLSyntaxError(message: "\(command) needs an id, e.g. \(command) a")
        }
        guard args.points.count <= 1 else { throw DSLSyntaxError(message: "only one position allowed") }

        var size = args.sizes.first
        if let diameter = args.numbers.first {
            guard kind == .circle else { throw DSLSyntaxError(message: "use WxH for the size, e.g. 12x4") }
            guard diameter > 0 else { throw DSLSyntaxError(message: "size must be positive") }
            size = GridSize(diameter, diameter)
        }
        guard args.sizes.count + args.numbers.count <= 1 else { throw DSLSyntaxError(message: "only one size allowed") }
        return .shape(kind, id: id, at: args.points.first, size: size, label: label, style: args.style)
    }

    private static func connector(arrow: Bool, _ args: Arguments) throws -> DSLStatement {
        let command = arrow ? "arrow" : "line"
        try args.reject(sizes: true, numbers: true, command: command)
        try args.rejectClosed(command)
        let label = try args.label()
        let usage = "use \(command) a>b or \(command) X,Y X,Y"

        if args.chains.count == 1, args.points.isEmpty, args.words.isEmpty {
            return .connect(args.chains[0], label: label, style: args.style)
        }
        if args.chains.isEmpty, args.words.isEmpty, args.points.count >= 2 {
            return .polyline(args.points, arrow: arrow, closed: false, label: label, style: args.style)
        }
        // `arrow a b` is forgiven and read as `arrow a>b`.
        if args.chains.isEmpty, args.points.isEmpty, args.words.count >= 2 {
            let link = DSLLink(startArrow: false, endArrow: arrow)
            let chain = DSLChain(ids: args.words, links: Array(repeating: link, count: args.words.count - 1))
            return .connect(chain, label: label, style: args.style)
        }
        throw DSLSyntaxError(message: usage)
    }

    private static func layout(_ kind: DSLLayout, _ args: Arguments) throws -> DSLStatement {
        let command = kind == .row ? "row" : kind == .col ? "col" : "flow"
        try args.reject(points: true, sizes: true, labels: true, command: command)
        try args.rejectClosed(command)
        guard args.numbers.count <= 1 else { throw DSLSyntaxError(message: "only one gap allowed") }
        if let gap = args.numbers.first, gap < 0 {
            throw DSLSyntaxError(message: "gap can't be negative")
        }
        guard args.style.isEmpty else { throw DSLSyntaxError(message: "\(command) takes no style") }

        let chain: DSLChain
        switch (args.chains.count, args.words.isEmpty) {
        case (1, true):
            chain = args.chains[0]
        case (0, false):
            chain = DSLChain(ids: args.words, links: Array(repeating: .forward, count: args.words.count - 1))
        case (0, true):
            throw DSLSyntaxError(message: "\(command) needs ids, e.g. \(command) a b c")
        default:
            throw DSLSyntaxError(message: "use either '\(command) a b c' or '\(command) a>b>c'")
        }
        return .layout(kind, chain, gap: args.numbers.first)
    }
}

/// A statement's arguments sorted by kind. Order between kinds doesn't matter,
/// so `box a "API" 0,0` and `box a 0,0 "API"` mean the same thing.
private struct Arguments {
    var words: [String] = []
    var labels: [String] = []
    var points: [GridPoint] = []
    var sizes: [GridSize] = []
    var numbers: [Double] = []
    var chains: [DSLChain] = []
    var style: DrawingStyle = []
    var closed = false

    private static let styles: [String: DrawingStyle] = ["dashed": .dashed, "thick": .thick, "bold": .bold]

    init(_ tokens: ArraySlice<DSLToken>) throws {
        for token in tokens {
            switch token {
            case .word(let word):
                if let style = Self.styles[word] {
                    self.style.insert(style)
                } else if word == "closed" {
                    closed = true
                } else {
                    words.append(word)
                }
            case .string(let label): labels.append(label)
            case .point(let point): points.append(point)
            case .size(let size): sizes.append(size)
            case .number(let number): numbers.append(number)
            case .chain(let chain): chains.append(chain)
            }
        }
    }

    func label() throws -> String? {
        guard labels.count <= 1 else { throw DSLSyntaxError(message: "only one \"label\" allowed") }
        return labels.first
    }

    func reject(
        chains rejectChains: Bool = false, points rejectPoints: Bool = false, sizes rejectSizes: Bool = false,
        numbers rejectNumbers: Bool = false, words rejectWords: Bool = false, labels rejectLabels: Bool = false,
        command: String
    ) throws {
        if rejectChains, let chain = chains.first {
            throw DSLSyntaxError(message: "\(command) can't link '\(chain.ids.joined(separator: ">"))', use arrow")
        }
        if rejectPoints, !points.isEmpty { throw DSLSyntaxError(message: "\(command) takes no position") }
        if rejectSizes, !sizes.isEmpty { throw DSLSyntaxError(message: "\(command) takes no size") }
        if rejectNumbers, let number = numbers.first {
            throw DSLSyntaxError(message: "unexpected number '\(Self.format(number))'")
        }
        if rejectWords, let word = words.first { throw DSLSyntaxError(message: "unexpected '\(word)'") }
        if rejectLabels, !labels.isEmpty { throw DSLSyntaxError(message: "\(command) takes no \"label\"") }
    }

    func rejectClosed(_ command: String) throws {
        if closed { throw DSLSyntaxError(message: "'closed' only applies to path") }
    }

    private static func format(_ number: Double) -> String {
        number == number.rounded() ? String(Int(number)) : String(number)
    }
}
