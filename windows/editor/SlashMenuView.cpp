#include "editor/SlashMenuView.h"

#include "render/Fonts.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>

namespace wp {

SlashMenuView::SlashMenuView(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setAccessibleName(QStringLiteral("Commands"));
}

void SlashMenuView::setState(const std::optional<SlashMenuState> &state)
{
    m_state = state;
    scrollSelectionIntoView();
    update();
}

QSize SlashMenuView::preferredSize() const
{
    const int rows = std::max(1, std::min(m_state ? int(m_state->items().size()) : 0, maxVisibleRows));
    return QSize(menuWidth + 2 * shadowMargin, headerHeight + rows * rowHeight + 2 * (padding + shadowMargin));
}

void SlashMenuView::scrollSelectionIntoView()
{
    if (!m_state)
        return;
    const int selected = m_state->selectedIndex();
    if (selected < m_firstVisibleRow)
        m_firstVisibleRow = selected;
    else if (selected >= m_firstVisibleRow + maxVisibleRows)
        m_firstVisibleRow = selected - maxVisibleRows + 1;
    m_firstVisibleRow = std::max(0, std::min(m_firstVisibleRow, std::max(0, int(m_state->items().size()) - maxVisibleRows)));
}

QRect SlashMenuView::card() const
{
    return rect().adjusted(shadowMargin, shadowMargin, -shadowMargin, -shadowMargin);
}

QRect SlashMenuView::rowRect(int row) const
{
    const QRect c = card();
    return QRect(c.left() + padding, c.top() + padding + headerHeight + (row - m_firstVisibleRow) * rowHeight, c.width() - 2 * padding,
                 rowHeight);
}

std::optional<int> SlashMenuView::rowAt(const QPoint &point) const
{
    if (!m_state)
        return std::nullopt;
    const int last = std::min(int(m_state->items().size()), m_firstVisibleRow + maxVisibleRows);
    for (int row = m_firstVisibleRow; row < last; ++row) {
        if (rowRect(row).contains(point))
            return row;
    }
    return std::nullopt;
}

void SlashMenuView::drawGlyph(QPainter &p, SlashCommand command, const QRect &tile) const
{
    QString text;
    switch (command) {
    case SlashCommand::heading1: text = "H1"; break;
    case SlashCommand::heading2: text = "H2"; break;
    case SlashCommand::heading3: text = "H3"; break;
    case SlashCommand::bulletList: text = QString(QChar(0x2022)); break;
    case SlashCommand::numberedList: text = "1."; break;
    case SlashCommand::checklist: text = QString(QChar(0x2611)); break;
    case SlashCommand::quote: text = QString(QChar(0x201C)); break;
    case SlashCommand::codeBlock: text = "</>"; break;
    case SlashCommand::divider: text = QString(QChar(0x2014)); break;
    case SlashCommand::drawing: text = QString(QChar(0x25A1)); break;
    case SlashCommand::flashcards: text = QString(QChar(0x25A4)); break;
    case SlashCommand::newPage: text = "+"; break;
    }
    p.setFont(fonts::system(13, QFont::DemiBold));
    p.setPen(palette().color(QPalette::Text));
    p.drawText(tile, Qt::AlignCenter, text);
}

void SlashMenuView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect c = card();
    // Soft shadow.
    for (int i = 0; i < shadowMargin - 2; ++i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 5));
        p.drawRoundedRect(QRectF(c).adjusted(-i, -i + 3, i, i + 3), 8 + i, 8 + i);
    }
    const QColor text = palette().color(QPalette::Text);
    QColor secondary = text;
    secondary.setAlphaF(0.6);
    QColor separator = text;
    separator.setAlphaF(0.2);
    p.setBrush(palette().color(QPalette::Window));
    p.setPen(separator);
    p.drawRoundedRect(QRectF(c).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    p.setFont(fonts::system(11.5, QFont::Medium));
    p.setPen(secondary);
    if (!m_state || m_state->items().isEmpty()) {
        p.drawText(QPointF(c.left() + 14, c.top() + padding + 20), QStringLiteral("No results"));
        return;
    }
    p.drawText(QPointF(c.left() + 14, c.top() + padding + 20), QStringLiteral("Basic blocks"));

    const int last = std::min(int(m_state->items().size()), m_firstVisibleRow + maxVisibleRows);
    for (int row = m_firstVisibleRow; row < last; ++row) {
        const SlashCommand command = m_state->items()[row];
        const QRect r = rowRect(row);
        if (row == m_state->selectedIndex() || row == m_hoverRow) {
            QColor fill = text;
            fill.setAlphaF(row == m_state->selectedIndex() ? 0.1 : 0.05);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(r, 5, 5);
        }
        const QRect tile(r.left() + 6, r.top() + 6, 32, 32);
        p.setPen(separator);
        p.setBrush(palette().color(QPalette::Base));
        p.drawRoundedRect(QRectF(tile).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        drawGlyph(p, command, tile);
        p.setFont(fonts::system(13.5));
        p.setPen(text);
        p.drawText(QPointF(tile.right() + 10, r.top() + 19), SlashCommands::title(command));
        p.setFont(fonts::system(11.5));
        p.setPen(secondary);
        p.drawText(QPointF(tile.right() + 10, r.top() + 36), SlashCommands::subtitle(command));
    }
}

void SlashMenuView::mouseMoveEvent(QMouseEvent *event)
{
    const auto row = rowAt(event->position().toPoint());
    if (row != m_hoverRow) {
        m_hoverRow = row;
        update();
    }
}

void SlashMenuView::leaveEvent(QEvent *)
{
    m_hoverRow.reset();
    update();
}

void SlashMenuView::mousePressEvent(QMouseEvent *event)
{
    if (const auto row = rowAt(event->position().toPoint())) {
        if (onChoose)
            onChoose(*row);
    }
}

void SlashMenuView::wheelEvent(QWheelEvent *event)
{
    if (!m_state || m_state->items().size() <= maxVisibleRows)
        return;
    const int dy = event->angleDelta().y();
    const int step = dy > 0 ? -1 : dy < 0 ? 1 : 0;
    m_firstVisibleRow = std::max(0, std::min(m_firstVisibleRow + step, int(m_state->items().size()) - maxVisibleRows));
    update();
}

} // namespace wp
