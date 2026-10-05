import Foundation

/// A fully resolved drawing: every shape has a frame and every line has its
/// points, so a renderer only has to stroke paths and place labels.
public struct DrawingScene: Equatable {
    public var shapes: [SceneShape] = []
    public var lines: [SceneLine] = []
    public var dimensions: [SceneDimension] = []
    public var groups: [SceneGroup] = []

    public init() {}

    public func shape(_ id: String) -> SceneShape? {
        shapes.first { $0.id == id }
    }

    public func group(_ id: String) -> SceneGroup? {
        groups.first { $0.id == id }
    }

    /// Everything the scene draws, or `.zero` for an empty scene. Labels drawn
    /// outside shapes (line and dimension labels) are not included.
    public var bounds: GridRect {
        var rects = shapes.map(\.frame) + groups.map(\.frame)
        rects += lines.compactMap { GridRect.enclosing($0.points) }
        rects += dimensions.compactMap { dimension in
            let offset = dimension.offsetLine
            return GridRect.enclosing([dimension.from, dimension.to, offset.from, offset.to])
        }
        guard let first = rects.first else { return .zero }
        return rects.dropFirst().reduce(first) { $0.union($1) }
    }
}

public struct DrawingStyle: OptionSet, Hashable {
    public let rawValue: UInt8

    public init(rawValue: UInt8) {
        self.rawValue = rawValue
    }

    public static let dashed = DrawingStyle(rawValue: 1 << 0)
    public static let thick = DrawingStyle(rawValue: 1 << 1)
    public static let bold = DrawingStyle(rawValue: 1 << 2)
}

public enum ShapeKind: String, Equatable {
    case box, circle, db, text
}

public struct SceneShape: Equatable {
    public var id: String
    public var kind: ShapeKind
    public var frame: GridRect
    public var label: String?
    public var style: DrawingStyle
}

/// A straight connector, polyline or path.
public struct SceneLine: Equatable {
    public var points: [GridPoint]
    public var startArrow: Bool
    public var endArrow: Bool
    /// Draw a segment from the last point back to the first.
    public var closed: Bool
    public var label: String?
    public var style: DrawingStyle
    /// Set when the line connects two shapes or groups.
    public var from: String?
    public var to: String?
}

/// A blueprint-style dimension line, drawn `offset` units beside the measured
/// segment with extension lines back to it.
public struct SceneDimension: Equatable {
    public static let defaultOffset = 1.5

    public var from: GridPoint
    public var to: GridPoint
    public var label: String
    public var offset: Double = SceneDimension.defaultOffset

    /// The measured segment moved `offset` units to its left (up, for a
    /// left-to-right segment).
    public var offsetLine: (from: GridPoint, to: GridPoint) {
        let dx = to.x - from.x, dy = to.y - from.y
        let length = (dx * dx + dy * dy).squareRoot()
        guard length > 0 else { return (from, to) }
        let nx = dy / length * offset, ny = -dx / length * offset
        return (GridPoint(from.x + nx, from.y + ny), GridPoint(to.x + nx, to.y + ny))
    }

    /// The default label: the length in grid units, without a trailing `.0`.
    static func lengthLabel(from: GridPoint, to: GridPoint) -> String {
        let dx = to.x - from.x, dy = to.y - from.y
        let length = ((dx * dx + dy * dy).squareRoot() * 10).rounded() / 10
        return length == length.rounded() ? String(Int(length)) : String(length)
    }
}

/// A labelled frame drawn around other shapes.
public struct SceneGroup: Equatable {
    public var id: String
    public var frame: GridRect
    public var label: String?
    public var members: [String]
    public var style: DrawingStyle
}

/// A problem in drawing source. The description is one short line, cheap to
/// send back to Claude: `line 3: unknown id 'x'`.
public struct DrawingError: Error, Equatable, CustomStringConvertible {
    public var line: Int
    public var message: String

    public init(line: Int, message: String) {
        self.line = line
        self.message = message
    }

    public var description: String { "line \(line): \(message)" }
}

/// The result of compiling drawing source. The scene contains everything that
/// compiled, so a drawing with a typo still renders its valid parts.
public struct CompiledDrawing: Equatable {
    public var scene: DrawingScene
    public var errors: [DrawingError]
}
