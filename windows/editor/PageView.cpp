#include "editor/PageView.h"

#include "editor/BlockTextView.h"
#include "editor/DeckBlockView.h"
#include "editor/DrawingBlockView.h"
#include "render/Fonts.h"
#include "render/SceneRenderer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace wp {

namespace {
constexpr double gridSpacing = SceneRenderer::unit * 2;
}

PageView::PageView(PageID pageID, const BlueprintPalette &palette, QWidget *parent)
    : QWidget(parent), m_pageID(pageID), m_palette(palette)
{
    setAccessibleName(QStringLiteral("Page"));
}

QRectF PageView::sheetRect() const
{
    const double inset = PageGeometry::shadowInset;
    return QRectF(rect()).adjusted(inset, inset, -inset, -inset);
}

void PageView::setBlockViews(const QList<QWidget *> &views)
{
    // A view that moved to another page in the meantime stays there.
    for (QWidget *view : std::as_const(m_blockViews)) {
        if (!views.contains(view) && view->parentWidget() == this) {
            view->hide();
            view->setParent(nullptr);
        }
    }
    for (QWidget *view : views) {
        if (view->parentWidget() != this) {
            view->setParent(this);
            view->show();
        }
    }
    m_blockViews = views;
}

double PageView::layoutBlocks(const PageGeometry &geometry)
{
    const double inset = PageGeometry::shadowInset;
    const double top = inset + geometry.topPadding;
    double y = top;
    const int textWidth = int(geometry.textWidth());
    if (isRealized) {
        for (int i = 0; i < m_blockViews.size(); ++i) {
            QWidget *view = m_blockViews[i];
            if (i > 0)
                y += geometry.blockSpacing;
            double height;
            if (auto *text = qobject_cast<BlockTextView *>(view)) {
                if (text->width() != textWidth)
                    text->setWrapWidth(textWidth);
                height = text->height();
            } else if (auto *drawing = qobject_cast<DrawingBlockView *>(view)) {
                height = drawing->heightForWidth(textWidth);
            } else if (auto *deck = qobject_cast<DeckBlockView *>(view)) {
                height = deck->heightForWidth(textWidth);
            } else {
                height = view->height();
            }
            const QRect frame(int(inset + geometry.padding), int(y), textWidth, int(height));
            if (view->geometry() != frame)
                view->setGeometry(frame);
            y += height;
        }
    } else {
        y += estimatedContentHeight;
    }
    const auto sheet = geometry.sheet(y - top);
    if (m_pageBreaks != sheet.breaks) {
        m_pageBreaks = sheet.breaks;
        update();
    }
    return sheet.height;
}

void PageView::setBlueprintPalette(const BlueprintPalette &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    update();
}

void PageView::setFollowsAnotherPage(bool follows)
{
    if (m_followsAnotherPage == follows)
        return;
    m_followsAnotherPage = follows;
    update();
}

void PageView::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF sheet = sheetRect();
    const double radius = PageGeometry::cornerRadius;

    if (!m_palette.drawsSheet) {
        // No sheet: the text sits on the canvas, and a short centred rule marks each page after the first.
        if (m_followsAnotherPage)
            p.fillRect(QRectF(std::round(sheet.center().x() - 24), 0, 48, 1), m_palette.pageEdge);
        drawPageBreaks(p, sheet);
        return;
    }

    // Soft shadow under the sheet.
    p.setPen(Qt::NoPen);
    if (m_palette.shadowOpacity > 0) {
        // Twelve rings at 6/255 each made the original 0.22 shadow; scale them to the palette's opacity.
        const QColor ring = QColor::fromRgbF(0, 0, 0, std::min(1.0, 6.0 / 255 * m_palette.shadowOpacity / 0.22));
        for (int i = 12; i >= 1; --i) {
            p.setBrush(ring);
            p.drawRoundedRect(sheet.adjusted(-i * 0.6, -i * 0.6 + 3, i * 0.6, i * 0.6 + 3), radius + i * 0.6, radius + i * 0.6);
        }
    }
    p.setBrush(m_palette.pageBackground);
    p.drawRoundedRect(sheet, radius, radius);

    if (m_palette.showsGrid)
        drawGrid(p, QRectF(event->rect()).intersected(sheet), sheet);

    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(m_palette.pageEdge);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(sheet.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    drawPageBreaks(p, sheet);
}

void PageView::drawGrid(QPainter &p, const QRectF &area, const QRectF &sheet)
{
    const double radius = PageGeometry::cornerRadius;
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(sheet, radius, radius);
    p.setClipPath(clip);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(m_palette.grid, 1));
    for (double x = sheet.left() + (std::floor((area.left() - sheet.left()) / gridSpacing) + 1) * gridSpacing; x < area.right(); x += gridSpacing)
        p.drawLine(QPointF(x + 0.5, area.top()), QPointF(x + 0.5, area.bottom()));
    for (double y = sheet.top() + (std::floor((area.top() - sheet.top()) / gridSpacing) + 1) * gridSpacing; y < area.bottom(); y += gridSpacing)
        p.drawLine(QPointF(area.left(), y + 0.5), QPointF(area.right(), y + 0.5));
    p.restore();
}

void PageView::drawPageBreaks(QPainter &p, const QRectF &sheet)
{
    const QFont font = fonts::system(9.5, QFont::Medium);
    for (size_t index = 0; index < m_pageBreaks.size(); ++index) {
        const double y = std::round(sheet.top() + m_pageBreaks[index]) + 0.5;
        QPen pen(withAlpha(m_palette.text, 0.22), 1);
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 4});
        p.setPen(pen);
        p.drawLine(QPointF(sheet.left(), y), QPointF(sheet.right(), y));
        const QString label = QString::number(index + 2);
        p.setFont(font);
        p.setPen(withAlpha(m_palette.muted, 0.5));
        const QFontMetricsF metrics(font);
        p.drawText(QPointF(sheet.right() - metrics.horizontalAdvance(label) - 10, y + 3 + metrics.ascent()), label);
    }
}

void PageView::mousePressEvent(QMouseEvent *event)
{
    if (sheetRect().contains(event->position()) && onClickBelowBlocks)
        onClickBelowBlocks(this);
}

EditorDocumentView::EditorDocumentView(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
}

void EditorDocumentView::mouseMoveEvent(QMouseEvent *event)
{
    if (onMouseMoved)
        onMouseMoved(event->position());
}

void EditorDocumentView::leaveEvent(QEvent *)
{
    if (onMouseExited)
        onMouseExited();
}

} // namespace wp
