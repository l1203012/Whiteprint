#include "render/SceneRenderer.h"

#include "render/ScenePainter.h"

#include <QPainter>

namespace wp {

namespace SceneRenderer {

QSizeF canvasSize(const DrawingScene &scene)
{
    const GridRect bounds = scene.bounds();
    return QSizeF(bounds.size.width * unit + 2 * margin, bounds.size.height * unit + 2 * margin);
}

void draw(const DrawingScene &scene, QPainter &painter, const BlueprintPalette &palette)
{
    const GridRect bounds = scene.bounds();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.translate(margin - bounds.minX() * unit, margin - bounds.minY() * unit);
    ScenePainter(painter, palette).draw(scene);
    painter.restore();
}

} // namespace SceneRenderer

} // namespace wp
