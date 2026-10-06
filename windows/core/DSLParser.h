#pragma once
#include "core/DSLTokenizer.h"
#include "core/DrawingScene.h"
#include "core/Geometry.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace wp {

enum class DSLLayout { row, col, flow };

/// One parsed statement. Build with the static factories, which mirror the Swift enum cases;
/// only the fields of the statement's `kind` are meaningful.
struct DSLStatement {
    enum class Kind { shape, connect, polyline, dimension, group, layout };

    Kind kind = Kind::shape;
    // shape
    ShapeKind shapeKind = ShapeKind::box;
    std::optional<QString> id;
    std::optional<GridPoint> at;
    std::optional<GridSize> size;
    // shape, connect, polyline, dimension, group
    std::optional<QString> label;
    DrawingStyle style;
    // connect, layout
    DSLChain chain;
    // polyline
    QList<GridPoint> points;
    bool arrow = false;
    bool closed = false;
    // dimension
    GridPoint from;
    GridPoint to;
    // group
    QString groupID;
    QStringList members;
    // layout
    DSLLayout layoutKind = DSLLayout::row;
    std::optional<double> gap;

    static DSLStatement shape(ShapeKind kind, std::optional<QString> id, std::optional<GridPoint> at,
                              std::optional<GridSize> size, std::optional<QString> label, DrawingStyle style);
    /// `arrow a>b>c`: connectors between shapes or groups.
    static DSLStatement connect(DSLChain chain, std::optional<QString> label, DrawingStyle style);
    /// `line 0,0 10,0 10,5` or `path ... closed`: free-standing points.
    static DSLStatement polyline(QList<GridPoint> points, bool arrow, bool closed, std::optional<QString> label,
                                 DrawingStyle style);
    static DSLStatement dimension(GridPoint from, GridPoint to, std::optional<QString> label);
    static DSLStatement group(QString id, QStringList members, std::optional<QString> label, DrawingStyle style);
    /// For `row` and `col` only the chain's ids matter; `flow` also draws its links.
    static DSLStatement layout(DSLLayout kind, DSLChain chain, std::optional<double> gap);

    bool operator==(const DSLStatement &) const = default;
};

struct DSLParsedLine {
    int line = 0;
    DSLStatement statement;
    bool operator==(const DSLParsedLine &) const = default;
};

struct DSLParseResult {
    QList<DSLParsedLine> lines;
    QList<DrawingError> errors;
};

namespace DSLParser {

/// Drawings are meant to be small; this caps the work a single block can cause.
inline constexpr int maxStatements = 500;

/// Parses a whole drawing source. Lines with errors are reported and skipped.
DSLParseResult parse(const QString &source);

/// Turns one line's tokens into a statement. Throws DSLSyntaxError.
DSLStatement statement(const QList<DSLToken> &tokens);

} // namespace DSLParser

} // namespace wp
