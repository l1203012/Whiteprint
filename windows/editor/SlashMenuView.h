#pragma once
#include "editor/SlashCommand.h"

#include <QWidget>
#include <functional>
#include <optional>

namespace wp {

/// The `/` command menu: a floating list drawn from a `SlashMenuState`. It
/// never takes focus; the text view keeps typing and forwards arrows, Enter and Esc.
class SlashMenuView : public QWidget
{
public:
    static constexpr int menuWidth = 300;
    static constexpr int rowHeight = 44;
    static constexpr int headerHeight = 30;
    static constexpr int padding = 6;
    static constexpr int maxVisibleRows = 7;
    /// Room around the card for its shadow.
    static constexpr int shadowMargin = 14;

    explicit SlashMenuView(QWidget *parent = nullptr);

    const std::optional<SlashMenuState> &state() const { return m_state; }
    void setState(const std::optional<SlashMenuState> &state);
    std::function<void(int)> onChoose;

    QSize preferredSize() const;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    QRect card() const;
    QRect rowRect(int row) const;
    std::optional<int> rowAt(const QPoint &point) const;
    void scrollSelectionIntoView();
    void drawGlyph(QPainter &p, SlashCommand command, const QRect &tile) const;

    std::optional<SlashMenuState> m_state;
    int m_firstVisibleRow = 0;
    std::optional<int> m_hoverRow;
};

} // namespace wp
