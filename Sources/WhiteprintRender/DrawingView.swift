import AppKit
import WhiteprintCore

/// Renders drawing-language source. Flipped, sized to its content, transparent
/// background so the page grid shows through. Recompiles when `source` changes.
public final class DrawingView: NSView {
    /// Keeps an empty drawing tall enough to click.
    public static let minimumHeight: CGFloat = 40

    public var source: String {
        didSet {
            guard source != oldValue else { return }
            compile()
            needsDisplay = true
        }
    }
    public var palette: BlueprintPalette {
        didSet { needsDisplay = true }
    }
    /// Errors from the last compile.
    public private(set) var errors: [DrawingError] = []
    /// The scene from the last compile.
    public private(set) var scene = DrawingScene()

    public init(source: String, palette: BlueprintPalette = .blueprint) {
        self.source = source
        self.palette = palette
        super.init(frame: .zero)
        compile()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    public override var isFlipped: Bool { true }

    public override var isOpaque: Bool { false }

    public override var intrinsicContentSize: NSSize {
        let size = SceneRenderer.canvasSize(for: scene)
        return NSSize(width: size.width, height: max(size.height, Self.minimumHeight))
    }

    public override func draw(_ dirtyRect: NSRect) {
        guard let context = NSGraphicsContext.current?.cgContext else { return }
        SceneRenderer.draw(scene, in: context, palette: palette)
    }

    private func compile() {
        let compiled = DrawingCompiler.compile(source)
        scene = compiled.scene
        errors = compiled.errors
        invalidateIntrinsicContentSize()
    }
}
