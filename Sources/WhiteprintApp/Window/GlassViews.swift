import AppKit

/// Frosted, floating chrome: a blurred translucent fill, a hairline edge and
/// a soft shadow, in a capsule or a rounded rectangle. Stands in for macOS 26
/// Liquid Glass on the systems Whiteprint supports. With Reduce Transparency
/// on, the fill is opaque.
class GlassView: NSView {
    /// Corner radius; nil makes a capsule (half the height).
    var cornerRadius: CGFloat? {
        didSet { needsLayout = true }
    }

    private let blur = NSVisualEffectView()
    private let tint = NSView()

    init(cornerRadius: CGFloat? = nil) {
        self.cornerRadius = cornerRadius
        super.init(frame: .zero)
        wantsLayer = true
        layer?.masksToBounds = false
        layer?.shadowOpacity = 1
        layer?.shadowRadius = 8
        layer?.shadowOffset = CGSize(width: 0, height: -2)

        blur.material = .popover
        blur.blendingMode = .withinWindow
        blur.state = .active
        blur.wantsLayer = true
        blur.layer?.masksToBounds = true
        tint.wantsLayer = true
        for view in [blur, tint] {
            view.autoresizingMask = [.width, .height]
            view.frame = bounds
            addSubview(view)
        }
        NotificationCenter.default.addObserver(
            self, selector: #selector(accessibilityDisplayOptionsChanged),
            name: NSWorkspace.accessibilityDisplayOptionsDidChangeNotification, object: nil
        )
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    override var wantsUpdateLayer: Bool { true }

    private var radius: CGFloat {
        min(cornerRadius ?? bounds.height / 2, bounds.height / 2, bounds.width / 2)
    }

    override func layout() {
        super.layout()
        let radius = self.radius
        for view in [blur, tint] {
            view.layer?.cornerRadius = radius
            view.layer?.cornerCurve = .continuous
        }
        layer?.shadowPath = CGPath(roundedRect: bounds, cornerWidth: radius, cornerHeight: radius, transform: nil)
        needsDisplay = true
    }

    override func updateLayer() {
        let dark = effectiveAppearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua
        let opaque = NSWorkspace.shared.accessibilityDisplayShouldReduceTransparency
        let fill = dark ? NSColor(srgbRed: 50 / 255, green: 50 / 255, blue: 56 / 255, alpha: 1) : .white
        blur.isHidden = opaque
        tint.layer?.backgroundColor = fill.withAlphaComponent(opaque ? 1 : 0.78).cgColor
        tint.layer?.borderWidth = 1
        tint.layer?.borderColor = (dark ? NSColor(white: 1, alpha: 0.1) : NSColor(white: 0, alpha: 0.08)).cgColor
        layer?.shadowColor = NSColor(white: 0, alpha: dark ? 0.35 : 0.1).cgColor
    }

    @objc private func accessibilityDisplayOptionsChanged() {
        needsDisplay = true
    }
}

/// A borderless button for glass chrome: a symbol or short title, optionally
/// filled with the accent colour while `isOn`.
final class ChromeButton: NSButton {
    var isOn = false {
        didSet { if oldValue != isOn { needsDisplay = true } }
    }
    /// Fill the button with the accent colour while on (the `MD` lens), rather
    /// than tinting it softly (the sidebar toggle).
    var fillsWhenOn = false
    private var isHovered = false {
        didSet { if oldValue != isHovered { needsDisplay = true } }
    }
    private let size: NSSize

    init(symbol: String? = nil, title: String? = nil, size: NSSize, label: String, target: AnyObject?, action: Selector?) {
        self.size = size
        super.init(frame: NSRect(origin: .zero, size: size))
        self.target = target
        self.action = action
        isBordered = false
        setButtonType(.momentaryChange)
        if let symbol {
            image = NSImage(systemSymbolName: symbol, accessibilityDescription: label)?
                .withSymbolConfiguration(.init(pointSize: 14, weight: .regular))
            imagePosition = .imageOnly
        }
        self.title = title ?? ""
        font = .monospacedSystemFont(ofSize: 11.5, weight: .bold)
        toolTip = label
        setAccessibilityLabel(label)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var intrinsicContentSize: NSSize { size }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect], owner: self))
    }

    override func mouseEntered(with event: NSEvent) { isHovered = true }
    override func mouseExited(with event: NSEvent) { isHovered = false }

    override func draw(_ dirtyRect: NSRect) {
        let shape = NSBezierPath(roundedRect: bounds, xRadius: bounds.height / 2, yRadius: bounds.height / 2)
        let accent = NSColor.controlAccentColor
        if isOn {
            (fillsWhenOn ? accent : accent.withAlphaComponent(0.15)).setFill()
            shape.fill()
        } else if isHighlighted || isHovered {
            NSColor.labelColor.withAlphaComponent(isHighlighted ? 0.12 : 0.06).setFill()
            shape.fill()
        }
        let color: NSColor = isOn ? (fillsWhenOn ? .white : accent) : .labelColor
        if let image {
            let tinted = image.tinted(color)
            let origin = NSPoint(x: (bounds.width - image.size.width) / 2, y: (bounds.height - image.size.height) / 2)
            tinted.draw(in: NSRect(origin: origin, size: image.size).integral)
        } else {
            let attributes: [NSAttributedString.Key: Any] = [.font: font ?? .systemFont(ofSize: 12), .foregroundColor: color]
            let text = title as NSString
            let textSize = text.size(withAttributes: attributes)
            text.draw(at: NSPoint(x: (bounds.width - textSize.width) / 2, y: (bounds.height - textSize.height) / 2),
                      withAttributes: attributes)
        }
    }
}

private extension NSImage {
    /// A template symbol drawn in `color`.
    func tinted(_ color: NSColor) -> NSImage {
        NSImage(size: size, flipped: false) { rect in
            self.draw(in: rect)
            color.set()
            rect.fill(using: .sourceAtop)
            return true
        }
    }
}

extension GlassView {
    /// A 36 pt glass capsule around `views` laid out in a row, `padding`
    /// inside its edge so a filled button keeps a ring of glass around it.
    static func wrapping(_ views: [NSView], padding: CGFloat = 3) -> GlassView {
        let glass = GlassView()
        let stack = NSStackView(views: views)
        stack.spacing = 3
        stack.translatesAutoresizingMaskIntoConstraints = false
        glass.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: glass.leadingAnchor, constant: padding),
            stack.trailingAnchor.constraint(equalTo: glass.trailingAnchor, constant: -padding),
            stack.centerYAnchor.constraint(equalTo: glass.centerYAnchor),
            glass.heightAnchor.constraint(equalToConstant: 36),
        ])
        return glass
    }
}

/// Floats a view controller's view in a rounded glass panel below the
/// titlebar, inset from the window's edges: the sidebar. The inset on the
/// editor's side leaves room for the panel's shadow, which views clip on
/// macOS 13.
final class GlassPanelController: NSViewController {
    static let inset: CGFloat = 12
    private let content: NSViewController
    private var top: NSLayoutConstraint?

    /// The height of the hidden system tab bar, which the panel moves up into.
    var tabBarOffset: CGFloat = 0 {
        didSet { top?.constant = 4 - tabBarOffset }
    }

    init(content: NSViewController) {
        self.content = content
        super.init(nibName: nil, bundle: nil)
        addChild(content)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override func loadView() {
        let container = NSView()
        let panel = GlassView(cornerRadius: 24)
        panel.translatesAutoresizingMaskIntoConstraints = false
        content.view.translatesAutoresizingMaskIntoConstraints = false
        panel.addSubview(content.view)
        container.addSubview(panel)
        let inset = Self.inset
        NSLayoutConstraint.activate([
            panel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: inset),
            panel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -inset),
            panel.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -inset),
            content.view.leadingAnchor.constraint(equalTo: panel.leadingAnchor),
            content.view.trailingAnchor.constraint(equalTo: panel.trailingAnchor),
            content.view.topAnchor.constraint(equalTo: panel.topAnchor),
            content.view.bottomAnchor.constraint(equalTo: panel.bottomAnchor),
        ])
        top = panel.topAnchor.constraint(equalTo: container.safeAreaLayoutGuide.topAnchor, constant: 4 - tabBarOffset)
        top?.isActive = true
        view = container
    }
}

/// The window's split between the sidebar panel and the editor: no visible
/// divider, and Toggle Sidebar collapses the first pane.
final class ChromeSplitViewController: NSSplitViewController {
    override func loadView() {
        splitView = ClearDividerSplitView()
        splitView.isVertical = true
        splitView.dividerStyle = .thin
        super.loadView()
    }

    override func toggleSidebar(_ sender: Any?) {
        guard let sidebar = splitViewItems.first else { return }
        if NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            sidebar.isCollapsed.toggle()
        } else {
            sidebar.animator().isCollapsed.toggle()
        }
    }
}

private final class ClearDividerSplitView: NSSplitView {
    override var dividerColor: NSColor { .clear }
}
