#include "editor/BlockHandleView.h"

#include <QMouseEvent>
#include <QPainter>

namespace wp {

BlockHandleView::BlockHandleView(const BlueprintPalette &palette, QWidget *parent) : QWidget(parent), m_palette(palette)
{
    resize(handleSize());
    hide();
    setToolTip(QStringLiteral("Click for block options"));
    setAccessibleName(QStringLiteral("Block options"));
    setCursor(Qt::PointingHandCursor);
}

void BlockHandleView::setBlueprintPalette(const BlueprintPalette &palette)
{
    m_palette = palette;
    update();
}

void BlockHandleView::showFor(BlockID block, QPoint origin)
{
    move(origin);
    if (m_blockID == block && !isHidden())
        return;
    m_blockID = block;
    show();
    raise();
}

void BlockHandleView::hideHandle()
{
    hide();
    m_blockID.reset();
    m_hovered = false;
}

void BlockHandleView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_hovered) {
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(m_palette.text, 0.12));
        p.drawRoundedRect(QRectF(rect()), 4, 4);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(m_palette.muted, m_hovered ? 0.9 : 0.6));
    const double dot = 2.6;
    for (int column = 0; column < 2; ++column) {
        for (int row = 0; row < 3; ++row) {
            const double x = width() / 2.0 + (column == 0 ? -3.2 : 3.2) - dot / 2;
            const double y = height() / 2.0 + (row - 1) * 5 - dot / 2;
            p.drawEllipse(QRectF(x, y, dot, dot));
        }
    }
}

void BlockHandleView::enterEvent(QEnterEvent *)
{
    m_hovered = true;
    update();
}

void BlockHandleView::leaveEvent(QEvent *)
{
    m_hovered = false;
    update();
}

void BlockHandleView::mousePressEvent(QMouseEvent *)
{
    if (onClick)
        onClick(this);
}

} // namespace wp
