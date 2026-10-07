import AppKit
import WhiteprintRender

/// The `⋮⋮` handle shown beside the hovered block; click for the block menu.
final class BlockHandleView: NSView {
    static let size = NSSize(width: 18, height: 24)

    var palette: BlueprintPalette {
        didSet { needsDisplay = true }
    }
    var blockID: BlockID?
    var onClick: ((BlockHandleView) -> Void)?
    private var isHovered = false {
        didSet { needsDisplay = true }
    }

    init(palette: BlueprintPalette) {
        self.palette = palette
        super.init(frame: NSRect(origin: .zero, size: Self.size))
        isHidden = true
        setAccessibilityRole(.button)
        setAccessibilityLabel("Block options")
        toolTip = "Click for block options"
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var isFlipped: Bool { true }

    /// Shows the handle for `block` at `origin`, fading in unless Reduce Motion is on.
    func show(for block: BlockID, at origin: NSPoint) {
        setFrameOrigin(origin)
        guard blockID != block || isHidden else { return }
        blockID = block
        isHidden = false
        if NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            alphaValue = 1
        } else {
            alphaValue = 0
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.12
                animator().alphaValue = 1
            }
        }
    }

    func hide() {
        isHidden = true
        blockID = nil
        isHovered = false
    }

    override func draw(_ dirtyRect: NSRect) {
        if isHovered {
            palette.text.withAlphaComponent(0.12).setFill()
            NSBezierPath(roundedRect: bounds, xRadius: 4, yRadius: 4).fill()
        }
        palette.muted.withAlphaComponent(isHovered ? 0.9 : 0.6).setFill()
        let dot: CGFloat = 2.6
        for column in 0..<2 {
            for row in 0..<3 {
                let x = bounds.midX + (column == 0 ? -3.2 : 3.2) - dot / 2
                let y = bounds.midY + CGFloat(row - 1) * 5 - dot / 2
                NSBezierPath(ovalIn: NSRect(x: x, y: y, width: dot, height: dot)).fill()
            }
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                                       owner: self))
    }

    override func mouseEntered(with event: NSEvent) { isHovered = true }
    override func mouseExited(with event: NSEvent) { isHovered = false }

    override func mouseDown(with event: NSEvent) {
        onClick?(self)
    }
}
