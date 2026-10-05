#pragma once
#include <QList>
#include <algorithm>
#include <optional>

namespace wp {

/// Drawing coordinates are in grid units (1 unit = 10 pt on the page).
/// The origin is the top-left corner and y grows downwards.
struct GridPoint {
    double x = 0;
    double y = 0;

    GridPoint() = default;
    GridPoint(double x, double y) : x(x), y(y) {}
    bool operator==(const GridPoint &) const = default;

    static GridPoint zero() { return GridPoint(0, 0); }
};

struct GridSize {
    double width = 0;
    double height = 0;

    GridSize() = default;
    GridSize(double width, double height) : width(width), height(height) {}
    bool operator==(const GridSize &) const = default;
};

struct GridRect {
    GridPoint origin;
    GridSize size;

    GridRect() = default;
    GridRect(GridPoint origin, GridSize size) : origin(origin), size(size) {}
    GridRect(double x, double y, double width, double height) : origin(x, y), size(width, height) {}
    bool operator==(const GridRect &) const = default;

    static GridRect zero() { return GridRect(0, 0, 0, 0); }

    double minX() const { return origin.x; }
    double minY() const { return origin.y; }
    double maxX() const { return origin.x + size.width; }
    double maxY() const { return origin.y + size.height; }
    double midX() const { return origin.x + size.width / 2; }
    double midY() const { return origin.y + size.height / 2; }
    GridPoint center() const { return GridPoint(midX(), midY()); }

    GridRect unite(const GridRect &other) const
    {
        const double lx = std::min(minX(), other.minX());
        const double ly = std::min(minY(), other.minY());
        return GridRect(lx, ly, std::max(maxX(), other.maxX()) - lx, std::max(maxY(), other.maxY()) - ly);
    }

    /// Positive values shrink the rect, negative values grow it.
    GridRect insetBy(double dx, double dy) const
    {
        return GridRect(minX() + dx, minY() + dy, size.width - 2 * dx, size.height - 2 * dy);
    }

    /// The smallest rect containing all `points`, or nullopt if there are none.
    static std::optional<GridRect> enclosing(const QList<GridPoint> &points)
    {
        if (points.isEmpty())
            return std::nullopt;
        GridRect rect(points.first(), GridSize(0, 0));
        for (qsizetype i = 1; i < points.size(); ++i)
            rect = rect.unite(GridRect(points[i], GridSize(0, 0)));
        return rect;
    }
};

} // namespace wp
