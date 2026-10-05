#pragma once
#include "editor/EditorDocument.h"
#include "render/Palette.h"

#include <QWidget>
#include <functional>
#include <optional>

namespace wp {

/// The drag handle shown beside the hovered block (six dots); click for the block menu.
class BlockHandleView : public QWidget
{
public:
    static QSize handleSize() { return QSize(18, 24); }

    explicit BlockHandleView(const BlueprintPalette &palette, QWidget *parent = nullptr);

    std::optional<BlockID> blockID() const { return m_blockID; }
    std::function<void(BlockHandleView *)> onClick;

    /// Shows the handle for `block` at `origin`.
    void showFor(BlockID block, QPoint origin);
    void hideHandle();

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    BlueprintPalette m_palette;
    std::optional<BlockID> m_blockID;
    bool m_hovered = false;
};

} // namespace wp
