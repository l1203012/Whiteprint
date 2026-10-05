#include "core/DrawingCompiler.h"

#include "core/TextUtil.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace wp {

namespace DrawingDefaults {

GridSize size(ShapeKind kind, const std::optional<QString> &label)
{
    QList<QStringView> lines;
    if (label)
        lines = QStringView(*label).split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    int longest = 0;
    for (const QStringView &line : lines)
        longest = std::max(longest, graphemeCount(line));
    const double textWidth = std::ceil(double(longest) * characterWidth);
    const double textHeight = double(lines.size()) * lineHeight;
    switch (kind) {
    case ShapeKind::box:
        return GridSize(std::max(boxSize.width, textWidth + 2), std::max(boxSize.height, textHeight + 2));
    case ShapeKind::circle:
        return GridSize(std::max(circleSize.width, textWidth + 2), std::max(circleSize.height, textHeight + 2));
    case ShapeKind::db:
        return GridSize(std::max(dbSize.width, textWidth + 2), std::max(dbSize.height, textHeight + 3));
    case ShapeKind::text:
        return GridSize(std::max(1.0, textWidth), std::max(lineHeight, textHeight));
    }
    return GridSize();
}

} // namespace DrawingDefaults

namespace {

struct Connection {
    int line = 0;
    QString from;
    QString to;
    DSLLink link;
    std::optional<QString> label;
    DrawingStyle style;
};

struct LayoutRequest {
    int line;
    DSLLayout kind;
    DSLChain chain;
    std::optional<double> gap;
};

struct GroupRequest {
    int line;
    QString id;
    QStringList members;
    std::optional<QString> label;
    DrawingStyle style;
};

class SceneBuilder
{
public:
    DrawingScene scene;
    QList<DrawingError> errors;

    /// Statements are applied in phases, so order in the source mostly doesn't
    /// matter: shapes, then layouts, then automatic placement, then groups
    /// (which need final positions), then connectors (which need groups).
    void build(const QList<DSLParsedLine> &lines)
    {
        QList<LayoutRequest> layouts;
        QList<GroupRequest> groups;
        QList<Connection> connections;

        for (const DSLParsedLine &parsed : lines) {
            if (parsed.statement.kind == DSLStatement::Kind::shape && parsed.statement.id)
                explicitIDs.insert(*parsed.statement.id);
        }
        for (const DSLParsedLine &parsed : lines) {
            const DSLStatement &s = parsed.statement;
            switch (s.kind) {
            case DSLStatement::Kind::shape:
                declareShape(s.shapeKind, s.id, s.at, s.size, s.label, s.style, parsed.line);
                break;
            case DSLStatement::Kind::layout:
                layouts.append({parsed.line, s.layoutKind, s.chain, s.gap});
                break;
            case DSLStatement::Kind::group:
                groups.append({parsed.line, s.groupID, s.members, s.label, s.style});
                break;
            case DSLStatement::Kind::connect:
                connections += makeConnections(s.chain, s.label, s.style, parsed.line);
                break;
            case DSLStatement::Kind::polyline: {
                SceneLine line;
                line.points = s.points;
                line.startArrow = false;
                line.endArrow = s.arrow;
                line.closed = s.closed;
                line.label = s.label;
                line.style = s.style;
                scene.lines.append(line);
                break;
            }
            case DSLStatement::Kind::dimension: {
                SceneDimension d;
                d.from = s.from;
                d.to = s.to;
                d.label = s.label ? *s.label : SceneDimension::lengthLabel(s.from, s.to);
                scene.dimensions.append(d);
                break;
            }
            }
        }

        QSet<QString> groupIDs;
        for (const GroupRequest &g : groups)
            groupIDs.insert(g.id);
        for (const LayoutRequest &layout : layouts) {
            QString groupHit;
            bool found = false;
            for (const QString &id : layout.chain.ids) {
                if (groupIDs.contains(id)) {
                    groupHit = id;
                    found = true;
                    break;
                }
            }
            if (found) {
                error(layout.line, QStringLiteral("can't lay out group '%1'").arg(groupHit));
                continue;
            }
            applyLayout(layout.kind, layout.chain, layout.gap.value_or(DrawingDefaults::gap), layout.line);
            if (layout.kind == DSLLayout::flow)
                connections += makeConnections(layout.chain, std::nullopt, DrawingStyle(), layout.line);
        }
        placeRemainingShapes();
        for (const GroupRequest &group : groups)
            declareGroup(group.id, group.members, group.label, group.style, group.line);
        for (const Connection &connection : connections)
            connect(connection);
    }

private:
    QHash<QString, int> shapeIndex;
    QHash<QString, int> groupIndex;
    QSet<QString> placed;
    QHash<QString, int> definedOn;
    QSet<QString> explicitIDs;
    int unnamedTextCount = 0;

    // MARK: Shapes

    void declareShape(ShapeKind kind, std::optional<QString> idOpt, std::optional<GridPoint> at,
                      std::optional<GridSize> size, std::optional<QString> label, DrawingStyle style, int line)
    {
        const QString id = idOpt ? *idOpt : nextUnnamedTextID();
        if (definedOn.contains(id)) {
            error(line, QStringLiteral("'%1' is already defined on line %2").arg(id).arg(definedOn.value(id)));
            return;
        }
        definedOn.insert(id, line);
        if (!label)
            label = defaultLabel(id, kind);
        SceneShape shape;
        shape.id = id;
        shape.kind = kind;
        shape.frame = GridRect(at ? *at : GridPoint::zero(), size ? *size : DrawingDefaults::size(kind, label));
        shape.label = label;
        shape.style = style;
        shapeIndex.insert(id, int(scene.shapes.size()));
        scene.shapes.append(shape);
        if (at)
            placed.insert(id);
    }

    /// A shape without a label shows its id (`_` as space), so `db Postgres`
    /// needs no `"Postgres"`. Handle-style ids such as `a` or `b2` stay blank.
    static std::optional<QString> defaultLabel(const QString &id, ShapeKind kind)
    {
        if (kind == ShapeKind::text || id.isEmpty())
            return std::nullopt;
        int firstLength = 1;
        const char32_t first = codePointAt(id, 0, &firstLength);
        if (!isLetterCodePoint(first))
            return std::nullopt;
        bool allNumbers = true;
        qsizetype i = firstLength;
        while (i < id.size()) {
            int length = 1;
            const char32_t c = codePointAt(id, i, &length);
            i += length;
            if (!isNumberCodePoint(c)) {
                allNumbers = false;
                break;
            }
        }
        if (allNumbers)
            return std::nullopt;
        QString label = id;
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        return label;
    }

    /// Unnamed text gets an internal `_tN` id, skipping any id the source uses itself.
    QString nextUnnamedTextID()
    {
        QString id;
        do {
            ++unnamedTextCount;
            id = QStringLiteral("_t%1").arg(unnamedTextCount);
        } while (explicitIDs.contains(id));
        return id;
    }

    void place(const QString &id, GridPoint origin)
    {
        auto it = shapeIndex.constFind(id);
        if (it == shapeIndex.constEnd())
            return;
        scene.shapes[it.value()].frame.origin = origin;
        placed.insert(id);
    }

    std::optional<GridRect> frameOf(const QString &id) const
    {
        if (auto it = shapeIndex.constFind(id); it != shapeIndex.constEnd())
            return scene.shapes[it.value()].frame;
        if (auto it = groupIndex.constFind(id); it != groupIndex.constEnd())
            return scene.groups[it.value()].frame;
        return std::nullopt;
    }

    /// The y where a new auto-placed row starts: below everything placed so far.
    double nextRowY() const
    {
        std::optional<double> best;
        auto consider = [&best](double v) {
            if (!best || v > *best)
                best = v;
        };
        for (const SceneShape &s : scene.shapes) {
            if (placed.contains(s.id))
                consider(s.frame.maxY());
        }
        for (const SceneLine &l : scene.lines) {
            for (const GridPoint &p : l.points)
                consider(p.y);
        }
        for (const SceneDimension &d : scene.dimensions) {
            consider(d.from.y);
            consider(d.to.y);
        }
        return best ? *best + DrawingDefaults::gap : 0;
    }

    // MARK: Layout

    /// Places the chain's unplaced shapes one after another. Ids that don't
    /// exist yet become boxes labelled with the id, so `flow Client>API>DB`
    /// needs no other lines. Shapes that already have a position stay put.
    void applyLayout(DSLLayout kind, const DSLChain &chain, double gap, int line)
    {
        for (const QString &id : chain.ids) {
            if (!shapeIndex.contains(id)) {
                QString label = id;
                label.replace(QLatin1Char('_'), QLatin1Char(' '));
                declareShape(ShapeKind::box, id, std::nullopt, std::nullopt, label, DrawingStyle(), line);
            }
        }
        if (chain.ids.isEmpty())
            return;
        const QString first = chain.ids.first();
        if (!placed.contains(first))
            place(first, GridPoint(0, nextRowY()));
        for (qsizetype i = 1; i < chain.ids.size(); ++i) {
            const QString &previous = chain.ids[i - 1];
            const QString &id = chain.ids[i];
            if (placed.contains(id))
                continue;
            const auto anchor = frameOf(previous);
            const auto target = frameOf(id);
            if (!anchor || !target)
                continue;
            const GridSize size = target->size;
            switch (kind) {
            case DSLLayout::row:
            case DSLLayout::flow:
                place(id, GridPoint(anchor->maxX() + gap, anchor->midY() - size.height / 2));
                break;
            case DSLLayout::col:
                place(id, GridPoint(anchor->midX() - size.width / 2, anchor->maxY() + gap));
                break;
            }
        }
    }

    /// Shapes without a position or layout go in one row below everything else.
    void placeRemainingShapes()
    {
        const double y = nextRowY();
        double x = 0;
        const QList<SceneShape> snapshot = scene.shapes;
        for (const SceneShape &shape : snapshot) {
            if (placed.contains(shape.id))
                continue;
            place(shape.id, GridPoint(x, y));
            x += shape.frame.size.width + DrawingDefaults::gap;
        }
    }

    // MARK: Groups

    void declareGroup(const QString &id, const QStringList &members, const std::optional<QString> &label,
                      DrawingStyle style, int line)
    {
        if (definedOn.contains(id)) {
            error(line, QStringLiteral("'%1' is already defined on line %2").arg(id).arg(definedOn.value(id)));
            return;
        }
        QList<GridRect> frames;
        for (const QString &member : members) {
            if (auto f = frameOf(member))
                frames.append(*f);
            else
                error(line, QStringLiteral("unknown id '%1'").arg(member));
        }
        if (frames.isEmpty())
            return;
        GridRect united = frames.first();
        for (qsizetype i = 1; i < frames.size(); ++i)
            united = united.unite(frames[i]);
        GridRect frame = united.insetBy(-DrawingDefaults::groupPadding, -DrawingDefaults::groupPadding);
        if (label) {
            frame.origin.y -= DrawingDefaults::groupLabelHeight;
            frame.size.height += DrawingDefaults::groupLabelHeight;
        }
        definedOn.insert(id, line);
        groupIndex.insert(id, int(scene.groups.size()));
        SceneGroup group;
        group.id = id;
        group.frame = frame;
        group.label = label;
        group.members = members;
        group.style = style.unionWith(DrawingStyle::dashed);
        scene.groups.append(group);
    }

    // MARK: Connectors

    static QList<Connection> makeConnections(const DSLChain &chain, const std::optional<QString> &label,
                                             DrawingStyle style, int line)
    {
        QList<Connection> result;
        const qsizetype count = std::min(chain.ids.size() - 1, chain.links.size());
        for (qsizetype i = 0; i < count; ++i) {
            Connection c;
            c.line = line;
            c.from = chain.ids[i];
            c.to = chain.ids[i + 1];
            c.link = chain.links[i];
            c.label = label;
            c.style = style;
            result.append(c);
        }
        return result;
    }

    void connect(const Connection &connection)
    {
        const auto fromFrame = frameOf(connection.from);
        if (!fromFrame)
            return error(connection.line, QStringLiteral("unknown id '%1'").arg(connection.from));
        const auto toFrame = frameOf(connection.to);
        if (!toFrame)
            return error(connection.line, QStringLiteral("unknown id '%1'").arg(connection.to));
        if (connection.from == connection.to)
            return error(connection.line, QStringLiteral("can't link '%1' to itself").arg(connection.from));
        const GridPoint start = edgePoint(*fromFrame, kindOf(connection.from), toFrame->center());
        const GridPoint end = edgePoint(*toFrame, kindOf(connection.to), fromFrame->center());
        SceneLine line;
        line.points = {start, end};
        line.startArrow = connection.link.startArrow;
        line.endArrow = connection.link.endArrow;
        line.closed = false;
        line.label = connection.label;
        line.style = connection.style;
        line.from = connection.from;
        line.to = connection.to;
        scene.lines.append(line);
    }

    ShapeKind kindOf(const QString &id) const
    {
        auto it = shapeIndex.constFind(id);
        return it == shapeIndex.constEnd() ? ShapeKind::box : scene.shapes[it.value()].kind;
    }

    /// Where a line from the centre of `frame` towards `target` leaves the shape.
    static GridPoint edgePoint(const GridRect &frame, ShapeKind kind, GridPoint target)
    {
        const GridPoint center = frame.center();
        const double dx = target.x - center.x, dy = target.y - center.y;
        const double halfWidth = frame.size.width / 2, halfHeight = frame.size.height / 2;
        if (!((dx != 0 || dy != 0) && halfWidth > 0 && halfHeight > 0))
            return center;
        double t;
        if (kind == ShapeKind::circle) {
            t = 1 / std::sqrt((dx / halfWidth) * (dx / halfWidth) + (dy / halfHeight) * (dy / halfHeight));
        } else {
            const double inf = std::numeric_limits<double>::infinity();
            t = std::min(dx == 0 ? inf : halfWidth / std::abs(dx), dy == 0 ? inf : halfHeight / std::abs(dy));
        }
        return GridPoint(center.x + dx * t, center.y + dy * t);
    }

    void error(int line, const QString &message) { errors.append(DrawingError(line, message)); }
};

} // namespace

namespace DrawingCompiler {

CompiledDrawing compile(const QString &source)
{
    const DSLParseResult parsed = DSLParser::parse(source);
    SceneBuilder builder;
    builder.build(parsed.lines);
    QList<DrawingError> errors = parsed.errors;
    errors += builder.errors;
    std::stable_sort(errors.begin(), errors.end(),
                     [](const DrawingError &a, const DrawingError &b) { return a.line < b.line; });
    CompiledDrawing result;
    result.scene = builder.scene;
    result.errors = errors;
    return result;
}

} // namespace DrawingCompiler

} // namespace wp
