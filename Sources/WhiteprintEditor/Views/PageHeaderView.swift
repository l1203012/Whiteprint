import AppKit
import WhiteprintRender

/// The top of a note's first page, Notion-style: the cover banner, the page
/// icon half over it, and the Add icon / Add cover buttons that appear on hover.
///
/// It spans the sheet's width and replaces the sheet's top padding, so the
/// first block starts right below it.
final class PageHeaderView: NSView {
    static let iconSize: CGFloat = 72
    static let minCoverHeight: CGFloat = 120
    static let maxCoverHeight: CGFloat = 220
    /// Rounding of a cover on a sheetless page; on a sheet it follows the sheet's corners.
    static let coverCornerRadius: CGFloat = 10

    var palette: BlueprintPalette {
        didSet { applyColors() }
    }
    /// A cover gallery id; an unknown one shows no banner.
    var cover: String? {
        didSet {
            guard cover != oldValue else { return }
            coverImage = cover.flatMap(CoverGallery.image(id:))
            updateButtons()
        }
    }
    var icon: String? {
        didSet {
            guard icon != oldValue else { return }
            updateButtons()
            needsDisplay = true
        }
    }
    /// The left edge of the text column, for the icon and the buttons.
    var contentInset: CGFloat = 0 {
        didSet { if contentInset != oldValue { needsLayout = true } }
    }
    /// Space between the header's content and the first block.
    var topPadding: CGFloat = 0 {
        didSet { if topPadding != oldValue { needsLayout = true } }
    }

    /// Called with the icon's rect to edit (or add) the icon.
    var onEditIcon: ((NSRect) -> Void)?
    var onAddCover: (() -> Void)?
    /// Called with the button's rect to pick another cover.
    var onChangeCover: ((NSRect) -> Void)?
    var onRemoveCover: (() -> Void)?

    private var coverImage: NSImage? {
        didSet { needsDisplay = true }
    }
    private let addIconButton = PageHeaderView.button("Add icon", symbol: "face.smiling")
    private let addCoverButton = PageHeaderView.button("Add cover", symbol: "photo")
    private let changeCoverButton = PageHeaderView.button("Change cover", symbol: nil, onImage: true)
    private let removeCoverButton = PageHeaderView.button("Remove", symbol: nil, onImage: true)
    private var isHovered = false {
        didSet { if isHovered != oldValue { updateButtons() } }
    }
    private var isIconHovered = false {
        didSet { if isIconHovered != oldValue { needsDisplay = true } }
    }

    init(palette: BlueprintPalette) {
        self.palette = palette
        super.init(frame: .zero)
        setAccessibilityRole(.group)
        setAccessibilityLabel("Page header")
        for (button, action) in [
            (addIconButton, #selector(addIcon(_:))), (addCoverButton, #selector(addCover(_:))),
            (changeCoverButton, #selector(changeCover(_:))), (removeCoverButton, #selector(removeCover(_:))),
        ] {
            button.target = self
            button.action = action
            addSubview(button)
        }
        applyColors()
        updateButtons()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var isFlipped: Bool { true }
    override var isOpaque: Bool { false }

    var hasCover: Bool { coverImage != nil }

    // MARK: Layout

    /// The banner's height for a sheet `width` wide.
    static func coverHeight(width: CGFloat) -> CGFloat {
        min(max((width * 0.26).rounded(), minCoverHeight), maxCoverHeight)
    }

    /// How tall the header is: the cover and icon, then `topPadding`.
    func height(forWidth width: CGFloat) -> CGFloat {
        contentHeight(forWidth: width) + topPadding
    }

    /// The bottom of the cover or the icon, whichever is lower.
    private func contentHeight(forWidth width: CGFloat) -> CGFloat {
        let cover = hasCover ? Self.coverHeight(width: width) : 0
        guard icon != nil else { return cover }
        return iconTop(coverHeight: cover) + Self.iconSize
    }

    /// Half over the cover, or a little below the top without one.
    private func iconTop(coverHeight: CGFloat) -> CGFloat {
        coverHeight > 0 ? coverHeight - Self.iconSize / 2 : 28
    }

    var coverRect: NSRect {
        hasCover ? NSRect(x: 0, y: 0, width: bounds.width, height: Self.coverHeight(width: bounds.width)) : .zero
    }

    var iconRect: NSRect {
        guard icon != nil else { return .zero }
        return NSRect(x: contentInset - 8, y: iconTop(coverHeight: coverRect.height), width: Self.iconSize, height: Self.iconSize)
    }

    override func layout() {
        super.layout()
        // The add buttons sit just above the first block, in the top padding.
        let rowY = contentHeight(forWidth: bounds.width) + max(8, topPadding - 40)
        var x = contentInset - 6
        for button in [addIconButton, addCoverButton] where !button.isHidden {
            button.sizeToFit()
            button.setFrameOrigin(NSPoint(x: x, y: rowY))
            x += button.frame.width + 4
        }
        let cover = coverRect
        var right = cover.maxX - 12
        for button in [removeCoverButton, changeCoverButton] {
            button.sizeToFit()
            right -= button.frame.width
            button.setFrameOrigin(NSPoint(x: right, y: cover.maxY - button.frame.height - 10))
            right -= 6
        }
    }

    private func updateButtons() {
        addIconButton.isHidden = !isHovered || icon != nil
        addCoverButton.isHidden = !isHovered || hasCover
        changeCoverButton.isHidden = !isHovered || !hasCover
        removeCoverButton.isHidden = !isHovered || !hasCover
        needsLayout = true
    }

    private func applyColors() {
        for button in [addIconButton, addCoverButton] {
            button.contentTintColor = palette.muted
            button.attributedTitle = NSAttributedString(string: button.title, attributes: [
                .foregroundColor: palette.muted, .font: NSFont.systemFont(ofSize: 13),
            ])
        }
        needsDisplay = true
    }

    private static func button(_ title: String, symbol: String?, onImage: Bool = false) -> NSButton {
        let button = NSButton(title: title, target: nil, action: nil)
        if onImage {
            // Readable on any painting.
            button.bezelStyle = .rounded
            button.controlSize = .small
            button.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        } else {
            button.isBordered = false
            button.image = symbol.flatMap { NSImage(systemSymbolName: $0, accessibilityDescription: nil) }
            button.imagePosition = .imageLeading
            button.imageHugsTitle = true
        }
        button.setButtonType(.momentaryPushIn)
        return button
    }

    // MARK: Drawing

    override func draw(_ dirtyRect: NSRect) {
        if let coverImage {
            drawCover(coverImage, in: coverRect)
        }
        if let icon {
            let rect = iconRect
            if isIconHovered {
                palette.text.withAlphaComponent(0.08).setFill()
                NSBezierPath(roundedRect: rect, xRadius: 8, yRadius: 8).fill()
            }
            let attributes: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 58)]
            let size = (icon as NSString).size(withAttributes: attributes)
            (icon as NSString).draw(at: NSPoint(x: rect.midX - size.width / 2, y: rect.midY - size.height / 2),
                                    withAttributes: attributes)
        }
    }

    /// The image scaled to fill `rect`, cropped around its centre, with the
    /// sheet's top corners (or all corners on a sheetless page).
    private func drawCover(_ image: NSImage, in rect: NSRect) {
        NSGraphicsContext.saveGraphicsState()
        defer { NSGraphicsContext.restoreGraphicsState() }
        let clip: NSBezierPath
        if palette.drawsSheet {
            let r = PageGeometry.cornerRadius
            clip = NSBezierPath()
            clip.move(to: NSPoint(x: rect.minX, y: rect.maxY))
            clip.line(to: NSPoint(x: rect.minX, y: rect.minY + r))
            clip.appendArc(withCenter: NSPoint(x: rect.minX + r, y: rect.minY + r), radius: r, startAngle: 180, endAngle: 270)
            clip.line(to: NSPoint(x: rect.maxX - r, y: rect.minY))
            clip.appendArc(withCenter: NSPoint(x: rect.maxX - r, y: rect.minY + r), radius: r, startAngle: 270, endAngle: 360)
            clip.line(to: NSPoint(x: rect.maxX, y: rect.maxY))
            clip.close()
        } else {
            clip = NSBezierPath(roundedRect: rect, xRadius: Self.coverCornerRadius, yRadius: Self.coverCornerRadius)
        }
        clip.addClip()
        image.draw(in: rect, from: Self.fillSource(imageSize: image.size, target: rect.size),
                   operation: .sourceOver, fraction: 1, respectFlipped: true, hints: [.interpolation: NSImageInterpolation.high.rawValue])
    }

    /// The part of an image of `imageSize` that fills `target` without
    /// distortion, centred.
    static func fillSource(imageSize: NSSize, target: NSSize) -> NSRect {
        guard imageSize.width > 0, imageSize.height > 0, target.width > 0, target.height > 0 else { return .zero }
        let scale = max(target.width / imageSize.width, target.height / imageSize.height)
        let size = NSSize(width: target.width / scale, height: target.height / scale)
        return NSRect(x: (imageSize.width - size.width) / 2, y: (imageSize.height - size.height) / 2,
                      width: size.width, height: size.height)
    }

    // MARK: Mouse

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .mouseMoved, .activeInKeyWindow, .inVisibleRect],
                                       owner: self))
    }

    override func mouseEntered(with event: NSEvent) {
        isHovered = true
    }

    override func mouseMoved(with event: NSEvent) {
        isHovered = true
        isIconHovered = iconRect.contains(convert(event.locationInWindow, from: nil))
    }

    override func mouseExited(with event: NSEvent) {
        isHovered = false
        isIconHovered = false
    }

    override func mouseDown(with event: NSEvent) {
        if iconRect.contains(convert(event.locationInWindow, from: nil)) {
            onEditIcon?(iconRect)
        } else {
            super.mouseDown(with: event)
        }
    }

    @objc private func addIcon(_ sender: NSButton) {
        onEditIcon?(sender.frame)
    }

    @objc private func addCover(_ sender: NSButton) {
        onAddCover?()
    }

    @objc private func changeCover(_ sender: NSButton) {
        onChangeCover?(sender.frame)
    }

    @objc private func removeCover(_ sender: NSButton) {
        onRemoveCover?()
    }
}
