import AppKit
import WhiteprintCore
import WhiteprintRender

/// The drawing popover's content: a monospaced source editor, a live
/// preview on a blueprint swatch and the compile errors. ⌘↩ commits; the
/// popover commits on any close (clicking outside, esc).
final class DrawingSourceEditor: NSViewController, NSTextViewDelegate, NSPopoverDelegate {
    static let width: CGFloat = 460

    let blockID: BlockID
    private let palette: BlueprintPalette
    private let onCommit: (String) -> Void
    private let sourceView = SourceTextView()
    private let preview: DrawingView
    private let previewBox = PreviewSwatch()
    private let errorsLabel = NSTextField(wrappingLabelWithString: "")
    private var committed = false
    weak var popover: NSPopover?

    var source: String { sourceView.string }

    init(blockID: BlockID, source: String, palette: BlueprintPalette, onCommit: @escaping (String) -> Void) {
        self.blockID = blockID
        self.palette = palette
        self.onCommit = onCommit
        preview = DrawingView(source: source, palette: palette)
        super.init(nibName: nil, bundle: nil)
        sourceView.string = source
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override func loadView() {
        let root = NSView(frame: NSRect(x: 0, y: 0, width: Self.width, height: 420))

        let title = NSTextField(labelWithString: "Drawing")
        title.font = .systemFont(ofSize: 13, weight: .semibold)
        let hint = NSTextField(labelWithString: "⌘↩ to apply")
        hint.font = .systemFont(ofSize: 11)
        hint.textColor = .secondaryLabelColor

        sourceView.font = .monospacedSystemFont(ofSize: 12.5, weight: .regular)
        sourceView.isRichText = false
        sourceView.allowsUndo = true
        sourceView.isAutomaticQuoteSubstitutionEnabled = false
        sourceView.isAutomaticDashSubstitutionEnabled = false
        sourceView.isAutomaticTextReplacementEnabled = false
        sourceView.isAutomaticSpellingCorrectionEnabled = false
        sourceView.textContainerInset = NSSize(width: 6, height: 6)
        sourceView.delegate = self
        sourceView.onCommit = { [weak self] in self?.close() }
        sourceView.isVerticallyResizable = true
        sourceView.autoresizingMask = [.width]
        sourceView.textContainer?.widthTracksTextView = true
        sourceView.setAccessibilityLabel("Drawing source")
        let sourceScroll = NSScrollView()
        sourceScroll.documentView = sourceView
        sourceScroll.hasVerticalScroller = true
        sourceScroll.borderType = .bezelBorder
        sourceScroll.translatesAutoresizingMaskIntoConstraints = false

        previewBox.palette = palette
        previewBox.addSubview(preview)
        previewBox.translatesAutoresizingMaskIntoConstraints = false

        errorsLabel.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        errorsLabel.textColor = .systemOrange
        errorsLabel.maximumNumberOfLines = 4

        let header = NSStackView(views: [title, NSView(), hint])
        let stack = NSStackView(views: [header, sourceScroll, previewBox, errorsLabel])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)
        stack.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            stack.topAnchor.constraint(equalTo: root.topAnchor),
            stack.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            header.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            sourceScroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            sourceScroll.heightAnchor.constraint(equalToConstant: 150),
            previewBox.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            previewBox.heightAnchor.constraint(equalToConstant: 180),
            errorsLabel.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
        ])
        view = root
        update()
    }

    override func viewDidAppear() {
        super.viewDidAppear()
        view.window?.makeFirstResponder(sourceView)
    }

    func textDidChange(_ notification: Notification) {
        update()
    }

    private func update() {
        preview.source = source
        previewBox.canvasSize = DrawingBlockView.canvasSize(for: source)
        previewBox.source = source
        let errors = DrawingCompiler.compile(source).errors
        errorsLabel.stringValue = errors.map(\.description).joined(separator: "\n")
        errorsLabel.isHidden = errors.isEmpty
        previewBox.needsLayout = true
        previewBox.needsDisplay = true
    }

    /// Closes the popover (which commits) or commits directly when not shown.
    func close() {
        if let popover, popover.isShown {
            popover.performClose(nil)
        } else {
            commit()
        }
    }

    /// Closes without committing (the drawing went away).
    func discard() {
        committed = true
        popover?.close()
    }

    func commit() {
        guard !committed else { return }
        committed = true
        onCommit(source)
    }

    func popoverDidClose(_ notification: Notification) {
        commit()
    }
}

/// ⌘↩ commits instead of inserting a newline.
private final class SourceTextView: NSTextView {
    var onCommit: (() -> Void)?

    override func keyDown(with event: NSEvent) {
        if event.keyCode == 36 || event.keyCode == 76, event.modifierFlags.contains(.command) {
            onCommit?()
            return
        }
        super.keyDown(with: event)
    }

    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        if event.keyCode == 36 || event.keyCode == 76, event.modifierFlags.contains(.command) {
            onCommit?()
            return true
        }
        return super.performKeyEquivalent(with: event)
    }
}

/// A blueprint swatch that centres the preview drawing, scaled to fit.
private final class PreviewSwatch: NSView {
    var palette = BlueprintPalette.blueprint
    var canvasSize: CGSize = .zero
    var source = ""

    override var isFlipped: Bool { true }

    override func layout() {
        super.layout()
        guard let drawing = subviews.first else { return }
        guard canvasSize.width > 0, canvasSize.height > 0 else {
            drawing.frame = .zero
            return
        }
        let area = bounds.insetBy(dx: 8, dy: 8)
        let scale = min(1, area.width / canvasSize.width, area.height / canvasSize.height)
        let size = NSSize(width: canvasSize.width * scale, height: canvasSize.height * scale)
        drawing.frame = NSRect(x: (bounds.width - size.width) / 2, y: (bounds.height - size.height) / 2,
                               width: size.width, height: size.height)
        drawing.bounds = NSRect(origin: .zero, size: canvasSize)
    }

    override func draw(_ dirtyRect: NSRect) {
        let path = NSBezierPath(roundedRect: bounds, xRadius: 6, yRadius: 6)
        palette.pageBackground.setFill()
        path.fill()
    }
}
