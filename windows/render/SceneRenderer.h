#pragma once
#include "core/DrawingScene.h"
#include "render/Palette.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>

class QPainter;

namespace wp {

namespace SceneRenderer {

/// Points per grid unit.
inline constexpr double unit = 10;
/// Space around the scene's bounds, in points.
inline constexpr double margin = 10;

/// Size of the canvas `draw` fills: the scene's bounds plus margins.
QSizeF canvasSize(const DrawingScene &scene);

/// Draws the scene with its bounds' top-left at (margin, margin).
/// `painter` must be y-down, which is Qt's default.
void draw(const DrawingScene &scene, QPainter &painter, const BlueprintPalette &palette);

} // namespace SceneRenderer

/// The point on the canvas, before the scene's margin offset.
inline QPointF canvasPoint(const GridPoint &p)
{
    return QPointF(p.x * SceneRenderer::unit, p.y * SceneRenderer::unit);
}

inline QRectF canvasRect(const GridRect &r)
{
    return QRectF(r.minX() * SceneRenderer::unit, r.minY() * SceneRenderer::unit, r.size.width * SceneRenderer::unit,
                  r.size.height * SceneRenderer::unit);
}

} // namespace wp
