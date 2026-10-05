import AppKit
import WhiteprintRender

/// One blueprint sheet: blue background, faint grid, rounded corners and a
/// soft shadow, with its blocks stacked inside the padding.
///
/// Pages far from the viewport aren't realized: they have no block views,
/// just an estimated height, until they scroll near.
final class PageView: NSView {
    static let gridSpacing = SceneRenderer.unit * 2

    let pageID: PageID
    private let palette: BlueprintPalette
    var isRealized = false
    /// Content height used while not realized, and the text width it was estimated for.
    var estimatedContentHeight: CGFloat = 0
    var estimatedWidth: CGFloat = 0
    /// Called for clicks on the sheet outside any block.
    var onClickBelowBlocks: ((PageView) -> Void)?

    private(set) var blockViews: [NSView] = []

    init(pageID: PageID, palette: BlueprintPalette) {
        self.pageID = pageID
        self.palette = palette
        super.init(frame: .zero)
        setAccessibilityRole(.group)
        setAccessibilityLabel("Page")
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var isFlipped: Bool { true }
    override var isOpaque: Bool { false }

    /// The sheet, inside the margin left for its shadow.
    var sheetRect: NSRect {
        bounds.insetBy(dx: PageGeometry.shadowInset, dy: PageGeometry.shadowInset)
    }

    func setBlockViews(_ views: [NSView]) {
        // A view that moved to another page in the meantime stays there.
        for view in blockViews where !views.contains(view) && view.superview === self {
            view.removeFromSuperview()
        }
        for view in views where view.superview !== self {
            addSubview(view)
        }
        blockViews = views
    }

    /// Lays the blocks out top to bottom and returns the sheet height.
    func layoutBlocks(_ geometry: PageGeometry) -> CGFloat {
        let inset = PageGeometry.shadowInset
        var y = inset + PageGeometry.topPadding
        if isRealized {
            for (i, view) in blockViews.enumerated() {
                if i > 0 { y += PageGeometry.blockSpacing }
                let height: CGFloat
                switch view {
                case let text as BlockTextView:
                    if text.frame.width != geometry.textWidth {
                        text.setFrameSize(NSSize(width: geometry.textWidth, height: text.frame.height))
                        text.sizeToFit()
                    }
                    height = text.frame.height
                case let drawing as DrawingBlockView:
                    height = drawing.height(forWidth: geometry.textWidth)
                default:
                    height = view.frame.height
                }
                view.setFrameOrigin(NSPoint(x: inset + geometry.padding, y: y))
                if view.frame.size != NSSize(width: geometry.textWidth, height: height) {
                    view.setFrameSize(NSSize(width: geometry.textWidth, height: height))
                }
                y += height
            }
        } else {
            y += estimatedContentHeight
        }
        y += PageGeometry.bottomPadding
        return max(y - inset, geometry.minPageHeight)
    }

    override func draw(_ dirtyRect: NSRect) {
        let sheet = sheetRect
        let path = NSBezierPath(roundedRect: sheet, xRadius: PageGeometry.cornerRadius, yRadius: PageGeometry.cornerRadius)

        NSGraphicsContext.saveGraphicsState()
        let shadow = NSShadow()
        shadow.shadowColor = NSColor.black.withAlphaComponent(0.22)
        shadow.shadowBlurRadius = 12
        shadow.shadowOffset = NSSize(width: 0, height: -3)
        shadow.set()
        palette.pageBackground.setFill()
        path.fill()
        NSGraphicsContext.restoreGraphicsState()

        NSGraphicsContext.saveGraphicsState()
        path.addClip()
        let area = dirtyRect.intersection(sheet)
        let grid = NSBezierPath()
        let spacing = Self.gridSpacing
        var x = sheet.minX + (((area.minX - sheet.minX) / spacing).rounded(.down) + 1) * spacing
        while x < area.maxX {
            grid.move(to: NSPoint(x: x + 0.5, y: area.minY))
            grid.line(to: NSPoint(x: x + 0.5, y: area.maxY))
            x += spacing
        }
        var y = sheet.minY + (((area.minY - sheet.minY) / spacing).rounded(.down) + 1) * spacing
        while y < area.maxY {
            grid.move(to: NSPoint(x: area.minX, y: y + 0.5))
            grid.line(to: NSPoint(x: area.maxX, y: y + 0.5))
            y += spacing
        }
        grid.lineWidth = 1
        palette.grid.setStroke()
        grid.stroke()
        NSGraphicsContext.restoreGraphicsState()

        NSColor(white: 1, alpha: 0.08).setStroke()
        let edge = NSBezierPath(roundedRect: sheet.insetBy(dx: 0.5, dy: 0.5),
                                xRadius: PageGeometry.cornerRadius, yRadius: PageGeometry.cornerRadius)
        edge.stroke()
    }

    override func mouseDown(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        if sheetRect.contains(point) {
            onClickBelowBlocks?(self)
        }
    }
}

/// The scroll view's flipped document view; hosts the pages and the hover handle.
final class EditorDocumentView: NSView {
    var onMouseMoved: ((NSPoint) -> Void)?
    var onMouseExited: (() -> Void)?

    override var isFlipped: Bool { true }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                                       owner: self))
    }

    override func mouseMoved(with event: NSEvent) {
        onMouseMoved?(convert(event.locationInWindow, from: nil))
    }

    override func mouseExited(with event: NSEvent) {
        onMouseExited?()
    }
}
