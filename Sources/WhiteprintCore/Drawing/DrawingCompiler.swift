import Foundation

/// Compiles drawing language source into a `DrawingScene`.
///
/// The language is described in `docs/DSL.md`. Compiling never fails as a
/// whole: statements with errors are skipped and reported, the rest is drawn.
public enum DrawingCompiler {
    public static func compile(_ source: String) -> CompiledDrawing {
        let parsed = DSLParser.parse(source)
        var builder = SceneBuilder()
        builder.build(parsed.lines)
        let errors = (parsed.errors + builder.errors).enumerated()
            .sorted { ($0.element.line, $0.offset) < ($1.element.line, $1.offset) }
            .map(\.element)
        return CompiledDrawing(scene: builder.scene, errors: errors)
    }
}

/// Default sizes, in grid units.
enum DrawingDefaults {
    static let boxSize = GridSize(10, 4)
    static let circleSize = GridSize(4, 4)
    static let dbSize = GridSize(8, 5)
    static let gap = 4.0
    static let groupPadding = 1.5
    static let groupLabelHeight = 2.0
    /// Rough label character width and line height, used to grow shapes so
    /// their labels fit. The renderer measures text exactly.
    static let characterWidth = 0.8
    static let lineHeight = 2.0

    static func size(for kind: ShapeKind, label: String?) -> GridSize {
        let lines = label?.split(separator: "\n", omittingEmptySubsequences: false) ?? []
        let textWidth = ((Double(lines.map(\.count).max() ?? 0)) * characterWidth).rounded(.up)
        let textHeight = Double(lines.count) * lineHeight
        switch kind {
        case .box:
            return GridSize(max(boxSize.width, textWidth + 2), max(boxSize.height, textHeight + 2))
        case .circle:
            return GridSize(max(circleSize.width, textWidth + 2), max(circleSize.height, textHeight + 2))
        case .db:
            return GridSize(max(dbSize.width, textWidth + 2), max(dbSize.height, textHeight + 3))
        case .text:
            return GridSize(max(1, textWidth), max(lineHeight, textHeight))
        }
    }
}

private struct SceneBuilder {
    private(set) var scene = DrawingScene()
    private(set) var errors: [DrawingError] = []

    private var shapeIndex: [String: Int] = [:]
    private var groupIndex: [String: Int] = [:]
    private var placed: Set<String> = []
    private var definedOn: [String: Int] = [:]
    private var explicitIDs: Set<String> = []
    private var unnamedTextCount = 0

    private struct Connection {
        var line: Int
        var from: String
        var to: String
        var link: DSLLink
        var label: String?
        var style: DrawingStyle
    }

    /// Statements are applied in phases, so order in the source mostly doesn't
    /// matter: shapes, then layouts, then automatic placement, then groups
    /// (which need final positions), then connectors (which need groups).
    mutating func build(_ lines: [DSLParsedLine]) {
        var layouts: [(line: Int, kind: DSLLayout, chain: DSLChain, gap: Double?)] = []
        var groups: [(line: Int, id: String, members: [String], label: String?, style: DrawingStyle)] = []
        var connections: [Connection] = []

        for parsed in lines {
            if case .shape(_, let id?, _, _, _, _) = parsed.statement {
                explicitIDs.insert(id)
            }
        }
        for parsed in lines {
            switch parsed.statement {
            case let .shape(kind, id, at, size, label, style):
                declareShape(kind, id: id, at: at, size: size, label: label, style: style, line: parsed.line)
            case let .layout(kind, chain, gap):
                layouts.append((parsed.line, kind, chain, gap))
            case let .group(id, members, label, style):
                groups.append((parsed.line, id, members, label, style))
            case let .connect(chain, label, style):
                connections += Self.connections(chain, label: label, style: style, line: parsed.line)
            case let .polyline(points, arrow, closed, label, style):
                scene.lines.append(SceneLine(
                    points: points, startArrow: false, endArrow: arrow, closed: closed,
                    label: label, style: style, from: nil, to: nil
                ))
            case let .dimension(from, to, label):
                scene.dimensions.append(SceneDimension(
                    from: from, to: to, label: label ?? SceneDimension.lengthLabel(from: from, to: to)
                ))
            }
        }

        let groupIDs = Set(groups.map(\.id))
        for layout in layouts {
            if let group = layout.chain.ids.first(where: groupIDs.contains) {
                error(layout.line, "can't lay out group '\(group)'")
                continue
            }
            applyLayout(layout.kind, layout.chain, gap: layout.gap ?? DrawingDefaults.gap, line: layout.line)
            if layout.kind == .flow {
                connections += Self.connections(layout.chain, label: nil, style: [], line: layout.line)
            }
        }
        placeRemainingShapes()
        for group in groups {
            declareGroup(group.id, members: group.members, label: group.label, style: group.style, line: group.line)
        }
        for connection in connections {
            connect(connection)
        }
    }

    // MARK: Shapes

    private mutating func declareShape(
        _ kind: ShapeKind, id: String?, at: GridPoint?, size: GridSize?,
        label: String?, style: DrawingStyle, line: Int
    ) {
        let id = id ?? nextUnnamedTextID()
        if let first = definedOn[id] {
            error(line, "'\(id)' is already defined on line \(first)")
            return
        }
        definedOn[id] = line
        let label = label ?? Self.defaultLabel(for: id, kind: kind)
        let frame = GridRect(origin: at ?? .zero, size: size ?? DrawingDefaults.size(for: kind, label: label))
        shapeIndex[id] = scene.shapes.count
        scene.shapes.append(SceneShape(id: id, kind: kind, frame: frame, label: label, style: style))
        if at != nil {
            placed.insert(id)
        }
    }

    /// A shape without a label shows its id (`_` as space), so `db Postgres`
    /// needs no `"Postgres"`. Handle-style ids such as `a` or `b2` stay blank.
    private static func defaultLabel(for id: String, kind: ShapeKind) -> String? {
        guard kind != .text, let first = id.first, first.isLetter else { return nil }
        if id.dropFirst().allSatisfy(\.isNumber) { return nil }
        return id.replacingOccurrences(of: "_", with: " ")
    }

    /// Unnamed text gets an internal `_tN` id, skipping any id the source uses itself.
    private mutating func nextUnnamedTextID() -> String {
        var id: String
        repeat {
            unnamedTextCount += 1
            id = "_t\(unnamedTextCount)"
        } while explicitIDs.contains(id)
        return id
    }

    private mutating func place(_ id: String, at origin: GridPoint) {
        guard let i = shapeIndex[id] else { return }
        scene.shapes[i].frame.origin = origin
        placed.insert(id)
    }

    private func frame(of id: String) -> GridRect? {
        if let i = shapeIndex[id] { return scene.shapes[i].frame }
        if let i = groupIndex[id] { return scene.groups[i].frame }
        return nil
    }

    /// The y where a new auto-placed row starts: below everything placed so far.
    private var nextRowY: Double {
        var bottoms = scene.shapes.filter { placed.contains($0.id) }.map(\.frame.maxY)
        bottoms += scene.lines.flatMap { $0.points.map(\.y) }
        bottoms += scene.dimensions.flatMap { [$0.from.y, $0.to.y] }
        return bottoms.max().map { $0 + DrawingDefaults.gap } ?? 0
    }

    // MARK: Layout

    /// Places the chain's unplaced shapes one after another. Ids that don't
    /// exist yet become boxes labelled with the id, so `flow Client>API>DB`
    /// needs no other lines. Shapes that already have a position stay put.
    private mutating func applyLayout(_ kind: DSLLayout, _ chain: DSLChain, gap: Double, line: Int) {
        for id in chain.ids where shapeIndex[id] == nil {
            declareShape(
                .box, id: id, at: nil, size: nil,
                label: id.replacingOccurrences(of: "_", with: " "), style: [], line: line
            )
        }
        guard let first = chain.ids.first else { return }
        if !placed.contains(first) {
            place(first, at: GridPoint(0, nextRowY))
        }
        for (previous, id) in zip(chain.ids, chain.ids.dropFirst()) where !placed.contains(id) {
            guard let anchor = frame(of: previous), let size = frame(of: id)?.size else { continue }
            switch kind {
            case .row, .flow:
                place(id, at: GridPoint(anchor.maxX + gap, anchor.midY - size.height / 2))
            case .col:
                place(id, at: GridPoint(anchor.midX - size.width / 2, anchor.maxY + gap))
            }
        }
    }

    /// Shapes without a position or layout go in one row below everything else.
    private mutating func placeRemainingShapes() {
        let y = nextRowY
        var x = 0.0
        for shape in scene.shapes where !placed.contains(shape.id) {
            place(shape.id, at: GridPoint(x, y))
            x += shape.frame.size.width + DrawingDefaults.gap
        }
    }

    // MARK: Groups

    private mutating func declareGroup(
        _ id: String, members: [String], label: String?, style: DrawingStyle, line: Int
    ) {
        if let first = definedOn[id] {
            error(line, "'\(id)' is already defined on line \(first)")
            return
        }
        var frames: [GridRect] = []
        for member in members {
            if let frame = frame(of: member) {
                frames.append(frame)
            } else {
                error(line, "unknown id '\(member)'")
            }
        }
        guard let first = frames.first else { return }
        var frame = frames.dropFirst().reduce(first) { $0.union($1) }
            .insetBy(dx: -DrawingDefaults.groupPadding, dy: -DrawingDefaults.groupPadding)
        if label != nil {
            frame.origin.y -= DrawingDefaults.groupLabelHeight
            frame.size.height += DrawingDefaults.groupLabelHeight
        }
        definedOn[id] = line
        groupIndex[id] = scene.groups.count
        scene.groups.append(SceneGroup(
            id: id, frame: frame, label: label, members: members, style: style.union(.dashed)
        ))
    }

    // MARK: Connectors

    private static func connections(_ chain: DSLChain, label: String?, style: DrawingStyle, line: Int) -> [Connection] {
        zip(zip(chain.ids, chain.ids.dropFirst()), chain.links).map { pair, link in
            Connection(line: line, from: pair.0, to: pair.1, link: link, label: label, style: style)
        }
    }

    private mutating func connect(_ connection: Connection) {
        guard let fromFrame = frame(of: connection.from) else {
            return error(connection.line, "unknown id '\(connection.from)'")
        }
        guard let toFrame = frame(of: connection.to) else {
            return error(connection.line, "unknown id '\(connection.to)'")
        }
        guard connection.from != connection.to else {
            return error(connection.line, "can't link '\(connection.from)' to itself")
        }
        let start = edgePoint(of: fromFrame, kind: kind(of: connection.from), toward: toFrame.center)
        let end = edgePoint(of: toFrame, kind: kind(of: connection.to), toward: fromFrame.center)
        scene.lines.append(SceneLine(
            points: [start, end],
            startArrow: connection.link.startArrow, endArrow: connection.link.endArrow, closed: false,
            label: connection.label, style: connection.style, from: connection.from, to: connection.to
        ))
    }

    private func kind(of id: String) -> ShapeKind {
        shapeIndex[id].map { scene.shapes[$0].kind } ?? .box
    }

    /// Where a line from the centre of `frame` towards `target` leaves the shape.
    private func edgePoint(of frame: GridRect, kind: ShapeKind, toward target: GridPoint) -> GridPoint {
        let center = frame.center
        let dx = target.x - center.x, dy = target.y - center.y
        let halfWidth = frame.size.width / 2, halfHeight = frame.size.height / 2
        guard dx != 0 || dy != 0, halfWidth > 0, halfHeight > 0 else { return center }
        let t: Double
        if kind == .circle {
            t = 1 / ((dx / halfWidth) * (dx / halfWidth) + (dy / halfHeight) * (dy / halfHeight)).squareRoot()
        } else {
            t = min(dx == 0 ? .infinity : halfWidth / abs(dx), dy == 0 ? .infinity : halfHeight / abs(dy))
        }
        return GridPoint(center.x + dx * t, center.y + dy * t)
    }

    private mutating func error(_ line: Int, _ message: String) {
        errors.append(DrawingError(line: line, message: message))
    }
}
