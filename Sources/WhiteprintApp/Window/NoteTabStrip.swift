import AppKit

extension Notification.Name {
    /// Posted when a note window's tab title or edited state changes, so the
    /// other windows in its tab group redraw their tab strips.
    static let noteTabsDidChange = Notification.Name("WhiteprintNoteTabsDidChange")
}

/// The open notes of a window's tab group as one glass capsule in the
/// titlebar: a segment per note, the current one highlighted (with
/// "— Edited" while it has unsaved changes), and a + for a new note.
/// Replaces the system tab bar, which can't take the window's look.
///
/// Each tab is its own window with its own strip, so the strip that comes
/// forward slides its highlight over from where the last one had it.
final class NoteTabStrip: GlassView {
    /// The highlighted segment of the strip shown last, in any window.
    private static var lastSelectedIndex: Int?

    struct Tab: Equatable {
        let window: NSWindow
        let title: String
        let isEdited: Bool
    }

    var tabs: [Tab] = [] {
        didSet { if oldValue != tabs { rebuild() } }
    }
    weak var selected: NSWindow? {
        didSet { if oldValue !== selected { rebuild() } }
    }
    /// A click on a background tab.
    var onSelect: ((NSWindow) -> Void)?
    /// A click on the current tab, with the segment to anchor a menu to.
    var onClickSelected: ((NSView) -> Void)?

    private let stack = NSStackView()
    private let highlight = TabHighlight()
    private var isAnimating = false
    /// Toolbar items size to their view's constraints, not its intrinsic size.
    private lazy var width = widthAnchor.constraint(equalToConstant: 36)
    private lazy var addButton = ChromeButton(symbol: "plus", size: NSSize(width: 30, height: 30), label: "New Tab",
                                              target: nil, action: #selector(NSResponder.newWindowForTab(_:)))

    init() {
        super.init()
        stack.spacing = 2
        stack.edgeInsets = NSEdgeInsets(top: 3, left: 3, bottom: 3, right: 3)
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(highlight)
        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: trailingAnchor),
            stack.topAnchor.constraint(equalTo: topAnchor),
            stack.bottomAnchor.constraint(equalTo: bottomAnchor),
            heightAnchor.constraint(equalToConstant: 36),
            width,
        ])
        setAccessibilityRole(.tabGroup)
        setAccessibilityLabel("Open notes")
    }

    private func rebuild() {
        stack.arrangedSubviews.forEach { $0.removeFromSuperview() }
        let maxWidth: CGFloat = tabs.count > 1 ? 200 : 360
        for tab in tabs {
            let current = tab.window === selected
            let button = NoteTabButton(title: tab.title, isEdited: current && tab.isEdited, isCurrent: current,
                                       maxWidth: maxWidth)
            button.onClick = { [weak self] button in
                if current { self?.onClickSelected?(button) } else { self?.onSelect?(tab.window) }
            }
            stack.addArrangedSubview(button)
        }
        stack.addArrangedSubview(addButton)
        width.constant = ceil(stack.fittingSize.width)
        invalidateIntrinsicContentSize()
        needsLayout = true
    }

    private var selectedIndex: Int? {
        tabs.firstIndex { $0.window === selected }
    }

    /// The highlight's place behind the segment at `index`.
    private func highlightFrame(at index: Int) -> NSRect? {
        let segments = stack.arrangedSubviews.compactMap { $0 as? NoteTabButton }
        guard tabs.count > 1, segments.indices.contains(index) else { return nil }
        stack.layoutSubtreeIfNeeded()
        return convert(segments[index].frame, from: stack)
    }

    override var intrinsicContentSize: NSSize {
        NSSize(width: stack.fittingSize.width, height: 36)
    }

    override func layout() {
        super.layout()
        guard !isAnimating else { return }
        let frame = selectedIndex.flatMap(highlightFrame(at:))
        highlight.isHidden = frame == nil
        if let frame { highlight.frame = frame }
    }

    /// Called when this strip's window comes forward: slides the highlight
    /// from the segment the previous strip had highlighted.
    func selectionDidAppear() {
        defer { Self.lastSelectedIndex = selectedIndex }
        guard let index = selectedIndex, let previous = Self.lastSelectedIndex, previous != index,
              !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion,
              let from = highlightFrame(at: previous), let to = highlightFrame(at: index) else { return }
        highlight.isHidden = false
        highlight.frame = from
        isAnimating = true
        NSAnimationContext.runAnimationGroup { context in
            context.duration = 0.28
            context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            highlight.animator().frame = to
        } completionHandler: { [weak self] in
            self?.isAnimating = false
            self?.needsLayout = true
        }
    }
}

/// The soft capsule behind the current tab.
private final class TabHighlight: NSView {
    override func draw(_ dirtyRect: NSRect) {
        NSColor.labelColor.withAlphaComponent(0.08).setFill()
        NSBezierPath(roundedRect: bounds, xRadius: bounds.height / 2, yRadius: bounds.height / 2).fill()
    }
}

/// One note in the tab strip.
private final class NoteTabButton: NSView {
    var onClick: ((NoteTabButton) -> Void)?
    private let text: NSAttributedString
    private let isCurrent: Bool
    private let maxWidth: CGFloat
    private var isHovered = false {
        didSet { if oldValue != isHovered { needsDisplay = true } }
    }

    init(title: String, isEdited: Bool, isCurrent: Bool, maxWidth: CGFloat) {
        let text = NSMutableAttributedString(string: title, attributes: [
            .font: NSFont.systemFont(ofSize: 13, weight: isCurrent ? .semibold : .regular),
            .foregroundColor: isCurrent ? NSColor.labelColor : NSColor.secondaryLabelColor,
        ])
        if isEdited {
            text.append(NSAttributedString(string: "  — Edited", attributes: [
                .font: NSFont.systemFont(ofSize: 13), .foregroundColor: NSColor.secondaryLabelColor,
            ]))
        }
        let paragraph = NSMutableParagraphStyle()
        paragraph.lineBreakMode = .byTruncatingTail
        text.addAttribute(.paragraphStyle, value: paragraph, range: NSRange(location: 0, length: text.length))
        self.text = text
        self.isCurrent = isCurrent
        self.maxWidth = maxWidth
        super.init(frame: .zero)
        setContentHuggingPriority(.required, for: .horizontal)
        setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        setAccessibilityRole(.radioButton)
        setAccessibilityLabel(text.string)
        setAccessibilityValue(isCurrent)
        toolTip = title
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var intrinsicContentSize: NSSize {
        NSSize(width: min(maxWidth, ceil(text.size().width) + 24), height: 30)
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect], owner: self))
    }

    override func mouseEntered(with event: NSEvent) { isHovered = true }
    override func mouseExited(with event: NSEvent) { isHovered = false }

    override func mouseDown(with event: NSEvent) {
        onClick?(self)
    }

    override func accessibilityPerformPress() -> Bool {
        onClick?(self)
        return true
    }

    override func draw(_ dirtyRect: NSRect) {
        if isHovered && !isCurrent {
            NSColor.labelColor.withAlphaComponent(0.05).setFill()
            NSBezierPath(roundedRect: bounds, xRadius: bounds.height / 2, yRadius: bounds.height / 2).fill()
        }
        let height = text.size().height
        text.draw(with: NSRect(x: 12, y: (bounds.height - height) / 2, width: bounds.width - 24, height: height),
                  options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine])
    }
}
