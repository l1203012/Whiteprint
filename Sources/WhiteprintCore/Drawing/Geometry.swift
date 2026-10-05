/// Drawing coordinates are in grid units (1 unit = 10 pt on the page).
/// The origin is the top-left corner and y grows downwards.
public struct GridPoint: Equatable, Hashable {
    public var x: Double
    public var y: Double

    public init(_ x: Double, _ y: Double) {
        self.x = x
        self.y = y
    }

    public static let zero = GridPoint(0, 0)
}

public struct GridSize: Equatable, Hashable {
    public var width: Double
    public var height: Double

    public init(_ width: Double, _ height: Double) {
        self.width = width
        self.height = height
    }
}

public struct GridRect: Equatable, Hashable {
    public var origin: GridPoint
    public var size: GridSize

    public init(origin: GridPoint, size: GridSize) {
        self.origin = origin
        self.size = size
    }

    public init(x: Double, y: Double, width: Double, height: Double) {
        self.init(origin: GridPoint(x, y), size: GridSize(width, height))
    }

    public static let zero = GridRect(x: 0, y: 0, width: 0, height: 0)

    public var minX: Double { origin.x }
    public var minY: Double { origin.y }
    public var maxX: Double { origin.x + size.width }
    public var maxY: Double { origin.y + size.height }
    public var midX: Double { origin.x + size.width / 2 }
    public var midY: Double { origin.y + size.height / 2 }
    public var center: GridPoint { GridPoint(midX, midY) }

    public func union(_ other: GridRect) -> GridRect {
        let minX = Swift.min(self.minX, other.minX)
        let minY = Swift.min(self.minY, other.minY)
        return GridRect(
            x: minX, y: minY,
            width: Swift.max(maxX, other.maxX) - minX,
            height: Swift.max(maxY, other.maxY) - minY
        )
    }

    /// Positive values shrink the rect, negative values grow it.
    public func insetBy(dx: Double, dy: Double) -> GridRect {
        GridRect(x: minX + dx, y: minY + dy, width: size.width - 2 * dx, height: size.height - 2 * dy)
    }

    /// The smallest rect containing all `points`, or nil if there are none.
    static func enclosing<S: Sequence>(_ points: S) -> GridRect? where S.Element == GridPoint {
        var iterator = points.makeIterator()
        guard let first = iterator.next() else { return nil }
        var rect = GridRect(origin: first, size: GridSize(0, 0))
        while let point = iterator.next() {
            rect = rect.union(GridRect(origin: point, size: GridSize(0, 0)))
        }
        return rect
    }
}
