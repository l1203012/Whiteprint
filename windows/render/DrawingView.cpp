#include "render/DrawingView.h"

#include "core/DrawingCompiler.h"
#include "render/SceneRenderer.h"

#include <QPainter>
#include <cmath>

namespace wp {

DrawingView::DrawingView(const QString &source, BlueprintPalette palette, QWidget *parent)
    : QWidget(parent), m_source(source), m_palette(std::move(palette))
{
    setAttribute(Qt::WA_NoSystemBackground);
    compile();
}

void DrawingView::setSource(const QString &source)
{
    if (source == m_source)
        return;
    m_source = source;
    compile();
    update();
}

void DrawingView::setPalette(const BlueprintPalette &palette)
{
    m_palette = palette;
    update();
}

QSize DrawingView::sizeHint() const
{
    const QSizeF size = SceneRenderer::canvasSize(m_scene);
    return QSize(int(std::ceil(size.width())), int(std::ceil(std::max(size.height(), double(minimumHeight)))));
}

void DrawingView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    SceneRenderer::draw(m_scene, painter, m_palette);
}

void DrawingView::compile()
{
    const CompiledDrawing compiled = DrawingCompiler::compile(m_source);
    m_scene = compiled.scene;
    m_errors = compiled.errors;
    updateGeometry();
}

} // namespace wp
