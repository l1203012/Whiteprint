#pragma once
#include "editor/EditorDocument.h"
#include "render/DrawingView.h"
#include "render/Palette.h"

#include <QSizeF>
#include <QWidget>
#include <memory>

class QKeyEvent;

namespace wp {

class DrawingBlockView;

class DrawingBlockViewDelegate
{
public:
    virtual ~DrawingBlockViewDelegate() = default;
    virtual void drawingBlockViewRequestsEditor(DrawingBlockView *view) = 0;
    virtual bool drawingBlockViewHandleKey(DrawingBlockView *view, QKeyEvent *event) = 0;
};

/// An inline drawing: a centred `DrawingView`, an outline on hover, and an
/// accent outline when selected (focused). Double-click edits it.
class DrawingBlockView : public QWidget
{
    Q_OBJECT

public:
    static constexpr double verticalPadding = 10;
    static constexpr double emptyHeight = 72;

    DrawingBlockView(BlockID blockID, const QString &source, const BlueprintPalette &palette, QWidget *parent = nullptr);

    BlockID blockID() const { return m_blockID; }
    DrawingView *drawingView() const { return m_drawingView.get(); }
    void setDelegate(DrawingBlockViewDelegate *delegate) { m_delegate = delegate; }

    const QString &source() const { return m_drawingView->source(); }
    void setSource(const QString &source);

    /// The drawing's natural size; empty for an empty drawing.
    static QSizeF canvasSize(const QString &source);
    /// Height for a column of `width`, scaling a too-wide drawing down.
    static double heightFor(const QSizeF &size, double width);
    double heightForWidth(double width) const { return heightFor(m_canvasSize, width); }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

private:
    BlockID m_blockID;
    std::unique_ptr<DrawingView> m_drawingView;
    DrawingBlockViewDelegate *m_delegate = nullptr;
    BlueprintPalette m_palette;
    QSizeF m_canvasSize;
    bool m_hovered = false;
};

} // namespace wp
