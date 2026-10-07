import AppKit

/// The cover popover: the gallery as a grid of thumbnails, plus Random and Remove.
final class CoverPicker: NSViewController {
    private static let columns = 3
    private static let tileSize = NSSize(width: 150, height: 60)

    private let current: String?
    private let onChoose: (String?) -> Void
    weak var popover: NSPopover?

    /// `onChoose` gets a gallery id, or nil to remove the cover.
    init(current: String?, onChoose: @escaping (String?) -> Void) {
        self.current = current
        self.onChoose = onChoose
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override func loadView() {
        let title = NSTextField(labelWithString: "Gallery")
        title.font = .systemFont(ofSize: 13, weight: .semibold)
        let random = NSButton(title: "Random", target: self, action: #selector(chooseRandom(_:)))
        random.image = NSImage(systemSymbolName: "shuffle", accessibilityDescription: nil)
        random.imagePosition = .imageLeading
        random.controlSize = .small
        let remove = NSButton(title: "Remove", target: self, action: #selector(remove(_:)))
        remove.controlSize = .small
        remove.isEnabled = current != nil
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let bar = NSStackView(views: [title, spacer, random, remove])
        bar.spacing = 6

        let grid = NSGridView()
        grid.rowSpacing = 8
        grid.columnSpacing = 8
        let tiles = CoverGallery.covers.map { cover -> NSView in
            let tile = CoverTile(cover: cover, isSelected: cover.id == current)
            tile.onClick = { [weak self] in self?.choose(cover.id) }
            tile.widthAnchor.constraint(equalToConstant: Self.tileSize.width).isActive = true
            tile.heightAnchor.constraint(equalToConstant: Self.tileSize.height).isActive = true
            return tile
        }
        stride(from: 0, to: tiles.count, by: Self.columns).forEach { start in
            var row = Array(tiles[start..<min(start + Self.columns, tiles.count)])
            while row.count < Self.columns { row.append(NSGridCell.emptyContentView) }
            grid.addRow(with: row)
        }

        let stack = NSStackView(views: [bar, grid])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)
        bar.widthAnchor.constraint(equalTo: grid.widthAnchor).isActive = true
        view = stack
    }

    private func choose(_ id: String?) {
        popover?.performClose(nil)
        onChoose(id)
    }

    @objc private func chooseRandom(_ sender: Any?) {
        choose(CoverGallery.random(excluding: current).id)
    }

    @objc private func remove(_ sender: Any?) {
        choose(nil)
    }
}

/// One gallery thumbnail, filled and rounded, named in its tooltip.
private final class CoverTile: NSView {
    var onClick: (() -> Void)?
    private let image: NSImage?
    private let isSelected: Bool
    private var isHovered = false {
        didSet { needsDisplay = true }
    }

    init(cover: Cover, isSelected: Bool) {
        image = CoverGallery.image(id: cover.id)
        self.isSelected = isSelected
        super.init(frame: .zero)
        toolTip = cover.caption
        setAccessibilityElement(true)
        setAccessibilityRole(.button)
        setAccessibilityLabel(cover.caption)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        let path = NSBezierPath(roundedRect: bounds, xRadius: 5, yRadius: 5)
        NSGraphicsContext.saveGraphicsState()
        path.addClip()
        if let image {
            image.draw(in: bounds, from: PageHeaderView.fillSource(imageSize: image.size, target: bounds.size),
                       operation: .sourceOver, fraction: isHovered ? 0.85 : 1, respectFlipped: true, hints: nil)
        } else {
            NSColor.quaternaryLabelColor.setFill()
            bounds.fill()
        }
        NSGraphicsContext.restoreGraphicsState()
        if isSelected {
            NSColor.controlAccentColor.setStroke()
            let ring = NSBezierPath(roundedRect: bounds.insetBy(dx: 1.5, dy: 1.5), xRadius: 4, yRadius: 4)
            ring.lineWidth = 3
            ring.stroke()
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .activeAlways, .inVisibleRect], owner: self))
    }

    override func mouseEntered(with event: NSEvent) { isHovered = true }
    override func mouseExited(with event: NSEvent) { isHovered = false }

    override func mouseUp(with event: NSEvent) {
        if bounds.contains(convert(event.locationInWindow, from: nil)) { onClick?() }
    }

    override func mouseDown(with event: NSEvent) {}

    override func accessibilityPerformPress() -> Bool {
        onClick?()
        return true
    }
}

/// The icon popover: common emoji, a field the system emoji picker types
/// into for any other, and Remove.
final class IconPicker: NSViewController, NSTextFieldDelegate {
    static let common = [
        "📝", "📚", "📖", "🧠", "💡", "🎯", "✅", "📌",
        "🧭", "🗺️", "🚀", "⭐️", "🔥", "🌱", "🌊", "🏔️",
        "🎨", "🎵", "🧪", "🔬", "💻", "⚙️", "📊", "🧮",
        "📅", "✈️", "🏠", "💼", "❤️", "🙂", "🐱", "☕️",
    ]
    private static let columns = 8

    private let current: String?
    private let onChoose: (String?) -> Void
    private let field = NSTextField()
    weak var popover: NSPopover?

    /// `onChoose` gets an emoji, or nil to remove the icon.
    init(current: String?, onChoose: @escaping (String?) -> Void) {
        self.current = current
        self.onChoose = onChoose
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override func loadView() {
        let grid = NSGridView()
        grid.rowSpacing = 2
        grid.columnSpacing = 2
        let buttons = Self.common.map { emoji -> NSView in
            let button = NSButton(title: emoji, target: self, action: #selector(pick(_:)))
            button.isBordered = false
            button.font = .systemFont(ofSize: 22)
            button.setAccessibilityLabel(emoji)
            button.widthAnchor.constraint(equalToConstant: 34).isActive = true
            button.heightAnchor.constraint(equalToConstant: 34).isActive = true
            return button
        }
        stride(from: 0, to: buttons.count, by: Self.columns).forEach { start in
            grid.addRow(with: Array(buttons[start..<min(start + Self.columns, buttons.count)]))
        }

        field.placeholderString = "Type or pick an emoji"
        field.delegate = self
        field.controlSize = .small
        let more = NSButton(title: "All Emoji…", target: self, action: #selector(showCharacterPalette(_:)))
        more.controlSize = .small
        let remove = NSButton(title: "Remove", target: self, action: #selector(remove(_:)))
        remove.controlSize = .small
        remove.isEnabled = current != nil
        let bar = NSStackView(views: [field, more, remove])
        bar.spacing = 6

        let stack = NSStackView(views: [grid, bar])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 10, left: 10, bottom: 10, right: 10)
        bar.widthAnchor.constraint(equalTo: grid.widthAnchor).isActive = true
        view = stack
    }

    /// The first emoji in `text`, if any; a single emoji may be several scalars.
    static func firstEmoji(in text: String) -> String? {
        text.first { character in
            guard let scalar = character.unicodeScalars.first else { return false }
            return scalar.properties.isEmojiPresentation
                || (scalar.properties.isEmoji && character.unicodeScalars.count > 1)
        }.map(String.init)
    }

    func controlTextDidChange(_ obj: Notification) {
        if let emoji = Self.firstEmoji(in: field.stringValue) { choose(emoji) }
    }

    private func choose(_ icon: String?) {
        popover?.performClose(nil)
        onChoose(icon)
    }

    @objc private func pick(_ sender: NSButton) {
        choose(sender.title)
    }

    @objc private func showCharacterPalette(_ sender: Any?) {
        view.window?.makeFirstResponder(field)
        NSApp.orderFrontCharacterPalette(sender)
    }

    @objc private func remove(_ sender: Any?) {
        choose(nil)
    }
}
