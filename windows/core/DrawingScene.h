#pragma once
#include "core/Geometry.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <optional>
#include <utility>

namespace wp {

/// Bit set of line/shape styles (`dashed`, `thick`, `bold`).
struct DrawingStyle {
    uint8_t rawValue = 0;

    constexpr DrawingStyle() = default;
    constexpr explicit DrawingStyle(uint8_t rawValue) : rawValue(rawValue) {}

    static const DrawingStyle dashed;
    static const DrawingStyle thick;
    static const DrawingStyle bold;

    constexpr bool contains(DrawingStyle other) const { return (rawValue & other.rawValue) == other.rawValue; }
    constexpr bool isEmpty() const { return rawValue == 0; }
    constexpr void insert(DrawingStyle other) { rawValue |= other.rawValue; }
    constexpr DrawingStyle unionWith(DrawingStyle other) const { return DrawingStyle(rawValue | other.rawValue); }
    friend constexpr DrawingStyle operator|(DrawingStyle a, DrawingStyle b) { return a.unionWith(b); }
    constexpr bool operator==(const DrawingStyle &) const = default;
};
inline constexpr DrawingStyle DrawingStyle::dashed{uint8_t(1 << 0)};
inline constexpr DrawingStyle DrawingStyle::thick{uint8_t(1 << 1)};
inline constexpr DrawingStyle DrawingStyle::bold{uint8_t(1 << 2)};

enum class ShapeKind { box, circle, db, text };

/// The DSL keyword for a kind (`box`, `circle`, `db`, `text`), Swift's `rawValue`.
QString shapeKindName(ShapeKind kind);
std::optional<ShapeKind> shapeKindFromName(const QString &name);

struct SceneShape {
    QString id;
    ShapeKind kind = ShapeKind::box;
    GridRect frame;
    std::optional<QString> label;
    DrawingStyle style;
    bool operator==(const SceneShape &) const = default;
};

/// A straight connector, polyline or path.
struct SceneLine {
    QList<GridPoint> points;
    bool startArrow = false;
    bool endArrow = false;
    /// Draw a segment from the last point back to the first.
    bool closed = false;
    std::optional<QString> label;
    DrawingStyle style;
    /// Set when the line connects two shapes or groups.
    std::optional<QString> from;
    std::optional<QString> to;
    bool operator==(const SceneLine &) const = default;
};

/// A blueprint-style dimension line, drawn `offset` units beside the measured
/// segment with extension lines back to it.
struct SceneDimension {
    static constexpr double defaultOffset = 1.5;

    GridPoint from;
    GridPoint to;
    QString label;
    double offset = defaultOffset;

    /// The measured segment moved `offset` units to its left (up, for a left-to-right segment).
    std::pair<GridPoint, GridPoint> offsetLine() const;
    /// The default label: the length in grid units, without a trailing `.0`.
    static QString lengthLabel(GridPoint from, GridPoint to);
    bool operator==(const SceneDimension &) const = default;
};

/// A labelled frame drawn around other shapes.
struct SceneGroup {
    QString id;
    GridRect frame;
    std::optional<QString> label;
    QStringList members;
    DrawingStyle style;
    bool operator==(const SceneGroup &) const = default;
};

/// A fully resolved drawing: every shape has a frame and every line has its
/// points, so a renderer only has to stroke paths and place labels.
struct DrawingScene {
    QList<SceneShape> shapes;
    QList<SceneLine> lines;
    QList<SceneDimension> dimensions;
    QList<SceneGroup> groups;

    const SceneShape *shape(const QString &id) const;
    const SceneGroup *group(const QString &id) const;

    /// Everything the scene draws, or zero for an empty scene. Labels drawn
    /// outside shapes (line and dimension labels) are not included.
    GridRect bounds() const;
    bool operator==(const DrawingScene &) const = default;
};

/// A problem in drawing source. The description is one short line:
/// `line 3: unknown id 'x'`.
struct DrawingError {
    int line = 0;
    QString message;

    DrawingError() = default;
    DrawingError(int line, QString message) : line(line), message(std::move(message)) {}
    QString description() const;
    bool operator==(const DrawingError &) const = default;
};

/// The result of compiling drawing source. The scene contains everything that
/// compiled, so a drawing with a typo still renders its valid parts.
struct CompiledDrawing {
    DrawingScene scene;
    QList<DrawingError> errors;
    bool operator==(const CompiledDrawing &) const = default;
};

} // namespace wp
