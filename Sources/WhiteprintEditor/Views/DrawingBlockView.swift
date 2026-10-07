import AppKit
import WhiteprintCore
import WhiteprintRender

protocol DrawingBlockViewDelegate: AnyObject {
    func drawingBlockViewRequestsEditor(_ view: DrawingBlockView)
    func drawingBlockView(_ view: DrawingBlockView, handleKey event: NSEvent) -> Bool
}

/// An inline drawing: a centred `DrawingView`, an outline on hover, and an
/// accent outline when selected (first responder). Double-click edits it.
final class DrawingBlockView: NSView {
    static let verticalPadding: CGFloat = 10
    static let emptyHeight: CGFloat = 72

    let blockID: BlockID
    let drawingView: DrawingView
    weak var delegate: DrawingBlockViewDelegate?
    var palette: BlueprintPalette {
        didSet {
            drawingView.palette = palette
            needsDisplay = true
        }
    }
    private var canvasSize: CGSize = .zero
    private var isHovered = false {
        didSet { if oldValue != isHovered { needsDisplay = true } }
    }

    var source: String {
        get { drawingView.source }
        set {
            guard newValue != drawingView.source else { return }
            drawingView.source = newValue
            canvasSize = Self.canvasSize(for: newValue)
        }
    }

    init(blockID: BlockID, source: String, palette: BlueprintPalette) {
        self.blockID = blockID
        self.palette = palette
        drawingView = DrawingView(source: source, palette: palette)
        canvasSize = Self.canvasSize(for: source)
        super.init(frame: .zero)
        addSubview(drawingView)
        setAccessibilityRole(.image)
        setAccessibilityLabel("Drawing")
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    /// The drawing's natural size; `.zero` for an empty drawing.
    static func canvasSize(for source: String) -> CGSize {
        let scene = DrawingCompiler.compile(source).scene
        guard !scene.shapes.isEmpty || !scene.lines.isEmpty || !scene.dimensions.isEmpty || !scene.groups.isEmpty else {
            return .zero
        }
        return SceneRenderer.canvasSize(for: scene)
    }

    /// Height for a column of `width`, scaling a too-wide drawing down.
    static func height(for size: CGSize, width: CGFloat) -> CGFloat {
        guard size.width > 0, size.height > 0 else { return emptyHeight }
        let scale = min(1, width / size.width)
        return (size.height * scale).rounded(.up) + 2 * verticalPadding
    }

    func height(forWidth width: CGFloat) -> CGFloat {
        Self.height(for: canvasSize, width: width)
    }

    override func layout() {
        super.layout()
        guard canvasSize.width > 0 else {
            drawingView.frame = .zero
            return
        }
        let scale = min(1, bounds.width / canvasSize.width)
        let size = NSSize(width: (canvasSize.width * scale).rounded(), height: (canvasSize.height * scale).rounded())
        drawingView.frame = NSRect(x: ((bounds.width - size.width) / 2).rounded(), y: Self.verticalPadding,
                                   width: size.width, height: size.height)
        drawingView.bounds = NSRect(origin: .zero, size: canvasSize)
    }

    override func draw(_ dirtyRect: NSRect) {
        let selected = window?.firstResponder === self
        let outline = NSBezierPath(roundedRect: bounds.insetBy(dx: 1, dy: 1), xRadius: 6, yRadius: 6)
        if selected {
            palette.accent.withAlphaComponent(0.12).setFill()
            outline.fill()
            palette.accent.setStroke()
            outline.lineWidth = 1.5
            outline.stroke()
        } else if isHovered {
            palette.text.withAlphaComponent(0.22).setStroke()
            outline.stroke()
        }
        if canvasSize == .zero {
            var attributes = TextStyle.base(palette)
            attributes[.foregroundColor] = palette.muted
            let text = "Empty drawing — double-click to edit" as NSString
            let size = text.size(withAttributes: attributes)
            text.draw(at: NSPoint(x: (bounds.width - size.width) / 2, y: (bounds.height - size.height) / 2),
                      withAttributes: attributes)
        }
    }

    // MARK: Events

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                                       owner: self))
    }

    override func mouseEntered(with event: NSEvent) { isHovered = true }
    override func mouseExited(with event: NSEvent) { isHovered = false }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        if event.clickCount == 2 {
            delegate?.drawingBlockViewRequestsEditor(self)
        }
    }

    override func keyDown(with event: NSEvent) {
        if delegate?.drawingBlockView(self, handleKey: event) != true {
            super.keyDown(with: event)
        }
    }

    override func becomeFirstResponder() -> Bool {
        needsDisplay = true
        return true
    }

    override func resignFirstResponder() -> Bool {
        needsDisplay = true
        return true
    }
}
