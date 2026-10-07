#pragma once
#include "editor/EditorDocument.h"
#include "editor/PageGeometry.h"
#include "render/Palette.h"

#include <QList>
#include <QWidget>
#include <functional>
#include <vector>

namespace wp {

/// One sheet in the page theme's colours (blueprint pages add a faint grid),
/// with rounded corners and a soft shadow, its blocks stacked inside the
/// padding. In A4 layout, dashed guides mark where printed pages break.
///
/// Pages far from the viewport aren't realized: they have no block views,
/// just an estimated height, until they scroll near.
class PageView : public QWidget
{
public:
    PageView(PageID pageID, const BlueprintPalette &palette, QWidget *parent = nullptr);

    PageID pageID() const { return m_pageID; }
    const BlueprintPalette &blueprintPalette() const { return m_palette; }
    /// Changes the colours and repaints.
    void setBlueprintPalette(const BlueprintPalette &palette);
    bool isRealized = false;
    /// Not the note's first page: without a sheet, a short rule above marks the break.
    bool followsAnotherPage() const { return m_followsAnotherPage; }
    void setFollowsAnotherPage(bool follows);
    /// Content height used while not realized, and the text width it was estimated for.
    double estimatedContentHeight = 0;
    double estimatedWidth = 0;
    /// Called for clicks on the sheet outside any block.
    std::function<void(PageView *)> onClickBelowBlocks;

    const QList<QWidget *> &blockViews() const { return m_blockViews; }
    /// Offsets from the sheet top where printed pages break.
    const std::vector<double> &pageBreaks() const { return m_pageBreaks; }

    /// The sheet, inside the margin left for its shadow.
    QRectF sheetRect() const;

    void setBlockViews(const QList<QWidget *> &views);
    /// Lays the blocks out top to bottom and returns the sheet height.
    double layoutBlocks(const PageGeometry &geometry);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    void drawGrid(QPainter &painter, const QRectF &area, const QRectF &sheet);
    void drawPageBreaks(QPainter &painter, const QRectF &sheet);

    PageID m_pageID;
    BlueprintPalette m_palette;
    bool m_followsAnotherPage = false;
    QList<QWidget *> m_blockViews;
    std::vector<double> m_pageBreaks;
};

/// The scroll area's document widget; hosts the pages and the hover handle.
class EditorDocumentView : public QWidget
{
public:
    explicit EditorDocumentView(QWidget *parent = nullptr);
    std::function<void(QPointF)> onMouseMoved;
    std::function<void()> onMouseExited;

protected:
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
};

} // namespace wp
