#include "editor/DrawingBlockView.h"

#include "core/DrawingCompiler.h"
#include "editor/BlockTextView.h"
#include "render/Fonts.h"
#include "render/SceneRenderer.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

namespace wp {

DrawingBlockView::DrawingBlockView(BlockID blockID, const QString &source, const BlueprintPalette &palette, QWidget *parent)
    : QWidget(parent), m_blockID(blockID), m_drawingView(new DrawingView(source, palette)), m_palette(palette)
{
    m_canvasSize = canvasSize(source);
    // The drawing is painted by this view (scaled to the column), so the view itself stays unparented.
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("Drawing"));
}

void DrawingBlockView::setSource(const QString &source)
{
    if (source == m_drawingView->source())
        return;
    m_drawingView->setSource(source);
    m_canvasSize = canvasSize(source);
    update();
}

QSizeF DrawingBlockView::canvasSize(const QString &source)
{
    const DrawingScene scene = DrawingCompiler::compile(source).scene;
    if (scene.shapes.isEmpty() && scene.lines.isEmpty() && scene.dimensions.isEmpty() && scene.groups.isEmpty())
        return QSizeF();
    return SceneRenderer::canvasSize(scene);
}

double DrawingBlockView::heightFor(const QSizeF &size, double width)
{
    if (size.width() <= 0 || size.height() <= 0)
        return emptyHeight;
    const double scale = std::min(1.0, width / size.width());
    return std::ceil(size.height() * scale) + 2 * verticalPadding;
}

void DrawingBlockView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool selected = hasFocus();
    const QRectF outline = QRectF(rect()).adjusted(1, 1, -1, -1);
    if (selected) {
        p.setBrush(withAlpha(m_palette.accent, 0.12));
        p.setPen(QPen(m_palette.accent, 1.5));
        p.drawRoundedRect(outline, 6, 6);
    } else if (m_hovered) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(withAlpha(m_palette.text, 0.22), 1));
        p.drawRoundedRect(outline, 6, 6);
    }
    if (m_canvasSize.isEmpty()) {
        p.setFont(fonts::system(MarkdownStyler::defaultFontSize));
        p.setPen(m_palette.muted);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Empty drawing — double-click to edit"));
        return;
    }
    const double scale = std::min(1.0, width() / m_canvasSize.width());
    const double w = std::round(m_canvasSize.width() * scale);
    const double x = std::round((width() - w) / 2);
    p.translate(x, verticalPadding);
    p.scale(scale, scale);
    m_drawingView->resize(m_canvasSize.toSize());
    m_drawingView->render(&p, QPoint(), QRegion(), QWidget::DrawWindowBackground);
}

void DrawingBlockView::enterEvent(QEnterEvent *)
{
    m_hovered = true;
    update();
}

void DrawingBlockView::leaveEvent(QEvent *)
{
    m_hovered = false;
    update();
}

void DrawingBlockView::mousePressEvent(QMouseEvent *)
{
    setFocus(Qt::MouseFocusReason);
}

void DrawingBlockView::mouseDoubleClickEvent(QMouseEvent *)
{
    setFocus(Qt::MouseFocusReason);
    if (m_delegate)
        m_delegate->drawingBlockViewRequestsEditor(this);
}

void DrawingBlockView::keyPressEvent(QKeyEvent *event)
{
    if (!m_delegate || !m_delegate->drawingBlockViewHandleKey(this, event))
        QWidget::keyPressEvent(event);
}

void DrawingBlockView::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    update();
}

void DrawingBlockView::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    update();
}

} // namespace wp
