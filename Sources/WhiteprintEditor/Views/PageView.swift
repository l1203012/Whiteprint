import AppKit
import WhiteprintRender

/// One sheet in the page theme's colours (blueprint pages add a faint grid),
/// with rounded corners and a soft shadow, its blocks stacked inside the
/// padding. In A4 layout, dashed guides mark where printed pages break.
///
/// Pages far from the viewport aren't realized: they have no block views,
/// just an estimated height, until they scroll near.
final class PageView: NSView {
    static let gridSpacing = SceneRenderer.unit * 2

    let pageID: PageID
    var palette: BlueprintPalette {
        didSet { needsDisplay = true }
    }
    var isRealized = false
    /// Not the note's first page: without a sheet, a short rule above marks the break.
    var followsAnotherPage = false {
        didSet { if oldValue != followsAnotherPage { needsDisplay = true } }
    }
    /// Content height used while not realized, and the text width it was estimated for.
    var estimatedContentHeight: CGFloat = 0
    var estimatedWidth: CGFloat = 0
    /// Called for clicks on the sheet outside any block.
    var onClickBelowBlocks: ((PageView) -> Void)?

    private(set) var blockViews: [NSView] = []
    /// Offsets from the sheet top where printed pages break.
    private(set) var pageBreaks: [CGFloat] = [] {
        didSet { if oldValue != pageBreaks { needsDisplay = true } }
    }

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
        let top = inset + geometry.topPadding
        var y = top
        if isRealized {
            for (i, view) in blockViews.enumerated() {
                if i > 0 { y += geometry.blockSpacing }
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
                case let deck as DeckBlockView:
                    height = deck.height(forWidth: geometry.textWidth)
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
        let sheet = geometry.sheet(contentHeight: y - top)
        pageBreaks = sheet.breaks
        return sheet.height
    }

    override func draw(_ dirtyRect: NSRect) {
        let sheet = sheetRect
        guard palette.drawsSheet else {
            if followsAnotherPage {
                palette.pageEdge.setFill()
                NSRect(x: (sheet.midX - 24).rounded(), y: bounds.minY, width: 48, height: 1).fill()
            }
            drawPageBreaks(in: sheet)
            return
        }
        let path = NSBezierPath(roundedRect: sheet, xRadius: PageGeometry.cornerRadius, yRadius: PageGeometry.cornerRadius)

        NSGraphicsContext.saveGraphicsState()
        let shadow = NSShadow()
        shadow.shadowColor = NSColor.black.withAlphaComponent(palette.shadowOpacity)
        shadow.shadowBlurRadius = 12
        shadow.shadowOffset = NSSize(width: 0, height: -3)
        shadow.set()
        palette.pageBackground.setFill()
        path.fill()
        NSGraphicsContext.restoreGraphicsState()

        if palette.showsGrid {
            drawGrid(in: dirtyRect, sheet: sheet, clip: path)
        }

        palette.pageEdge.setStroke()
        let edge = NSBezierPath(roundedRect: sheet.insetBy(dx: 0.5, dy: 0.5),
                                xRadius: PageGeometry.cornerRadius, yRadius: PageGeometry.cornerRadius)
        edge.stroke()
        drawPageBreaks(in: sheet)
    }

    private func drawGrid(in dirtyRect: NSRect, sheet: NSRect, clip: NSBezierPath) {
        NSGraphicsContext.saveGraphicsState()
        clip.addClip()
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
    }

    /// A dashed line across the sheet at each printed page break, with the
    /// number of the page that starts there.
    private func drawPageBreaks(in sheet: NSRect) {
        let attributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 9.5, weight: .medium),
            .foregroundColor: palette.muted.withAlphaComponent(0.5),
        ]
        for (index, offset) in pageBreaks.enumerated() {
            let y = (sheet.minY + offset).rounded() + 0.5
            let line = NSBezierPath()
            line.move(to: NSPoint(x: sheet.minX, y: y))
            line.line(to: NSPoint(x: sheet.maxX, y: y))
            line.lineWidth = 1
            line.setLineDash([4, 4], count: 2, phase: 0)
            palette.text.withAlphaComponent(0.22).setStroke()
            line.stroke()
            let label = "\(index + 2)" as NSString
            let size = label.size(withAttributes: attributes)
            label.draw(at: NSPoint(x: sheet.maxX - size.width - 10, y: y + 3), withAttributes: attributes)
        }
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
