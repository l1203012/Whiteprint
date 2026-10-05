#include "core/DrawingScene.h"

#include "core/TextUtil.h"

#include <cmath>

namespace wp {

QString shapeKindName(ShapeKind kind)
{
    switch (kind) {
    case ShapeKind::box: return QStringLiteral("box");
    case ShapeKind::circle: return QStringLiteral("circle");
    case ShapeKind::db: return QStringLiteral("db");
    case ShapeKind::text: return QStringLiteral("text");
    }
    return {};
}

std::optional<ShapeKind> shapeKindFromName(const QString &name)
{
    for (ShapeKind k : {ShapeKind::box, ShapeKind::circle, ShapeKind::db, ShapeKind::text}) {
        if (shapeKindName(k) == name)
            return k;
    }
    return std::nullopt;
}

std::pair<GridPoint, GridPoint> SceneDimension::offsetLine() const
{
    const double dx = to.x - from.x, dy = to.y - from.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0))
        return {from, to};
    const double nx = dy / length * offset, ny = -dx / length * offset;
    return {GridPoint(from.x + nx, from.y + ny), GridPoint(to.x + nx, to.y + ny)};
}

QString SceneDimension::lengthLabel(GridPoint from, GridPoint to)
{
    const double dx = to.x - from.x, dy = to.y - from.y;
    const double length = std::round(std::sqrt(dx * dx + dy * dy) * 10) / 10;
    return formatDouble(length);
}

const SceneShape *DrawingScene::shape(const QString &id) const
{
    for (const SceneShape &s : shapes) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

const SceneGroup *DrawingScene::group(const QString &id) const
{
    for (const SceneGroup &g : groups) {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

GridRect DrawingScene::bounds() const
{
    QList<GridRect> rects;
    for (const SceneShape &s : shapes)
        rects.append(s.frame);
    for (const SceneGroup &g : groups)
        rects.append(g.frame);
    for (const SceneLine &l : lines) {
        if (auto r = GridRect::enclosing(l.points))
            rects.append(*r);
    }
    for (const SceneDimension &d : dimensions) {
        const auto offset = d.offsetLine();
        if (auto r = GridRect::enclosing({d.from, d.to, offset.first, offset.second}))
            rects.append(*r);
    }
    if (rects.isEmpty())
        return GridRect::zero();
    GridRect result = rects.first();
    for (qsizetype i = 1; i < rects.size(); ++i)
        result = result.unite(rects[i]);
    return result;
}

QString DrawingError::description() const
{
    return QStringLiteral("line %1: %2").arg(line).arg(message);
}

} // namespace wp
