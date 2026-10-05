import AppKit

/// The `/` command menu: a floating list drawn from a `SlashMenuState`. It
/// never takes focus; the text view keeps typing and forwards arrows, ↩ and esc.
final class SlashMenuView: NSView {
    static let width: CGFloat = 300
    static let rowHeight: CGFloat = 44
    static let headerHeight: CGFloat = 30
    static let padding: CGFloat = 6
    static let maxVisibleRows = 7
    /// Room around the card for its shadow.
    static let shadowMargin: CGFloat = 14

    var state: SlashMenuState? {
        didSet {
            scrollSelectionIntoView()
            needsDisplay = true
        }
    }
    var onChoose: ((Int) -> Void)?
    private var firstVisibleRow = 0
    private var hoverRow: Int?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setAccessibilityRole(.menu)
        setAccessibilityLabel("Commands")
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var isFlipped: Bool { true }

    var preferredSize: NSSize {
        let rows = max(1, min(state?.items.count ?? 0, Self.maxVisibleRows))
        return NSSize(width: Self.width + 2 * Self.shadowMargin,
                      height: Self.headerHeight + CGFloat(rows) * Self.rowHeight + 2 * (Self.padding + Self.shadowMargin))
    }

    private var visibleRows: Range<Int> {
        let count = state?.items.count ?? 0
        return firstVisibleRow..<min(count, firstVisibleRow + Self.maxVisibleRows)
    }

    private func scrollSelectionIntoView() {
        guard let state else { return }
        let selected = state.selectedIndex
        if selected < firstVisibleRow {
            firstVisibleRow = selected
        } else if selected >= firstVisibleRow + Self.maxVisibleRows {
            firstVisibleRow = selected - Self.maxVisibleRows + 1
        }
        firstVisibleRow = max(0, min(firstVisibleRow, max(0, state.items.count - Self.maxVisibleRows)))
    }

    private var card: NSRect {
        bounds.insetBy(dx: Self.shadowMargin, dy: Self.shadowMargin)
    }

    private func rowRect(_ row: Int) -> NSRect {
        let y = card.minY + Self.padding + Self.headerHeight + CGFloat(row - firstVisibleRow) * Self.rowHeight
        return NSRect(x: card.minX + Self.padding, y: y, width: card.width - 2 * Self.padding, height: Self.rowHeight)
    }

    override func draw(_ dirtyRect: NSRect) {
        let background = NSBezierPath(roundedRect: card.insetBy(dx: 0.5, dy: 0.5), xRadius: 8, yRadius: 8)
        NSGraphicsContext.saveGraphicsState()
        let shadow = NSShadow()
        shadow.shadowColor = NSColor.black.withAlphaComponent(0.22)
        shadow.shadowBlurRadius = 12
        shadow.shadowOffset = NSSize(width: 0, height: -4)
        shadow.set()
        NSColor.controlBackgroundColor.setFill()
        background.fill()
        NSGraphicsContext.restoreGraphicsState()
        NSColor.separatorColor.setStroke()
        background.stroke()

        let header: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 11.5, weight: .medium), .foregroundColor: NSColor.secondaryLabelColor,
        ]
        guard let state, !state.items.isEmpty else {
            ("No results" as NSString).draw(at: NSPoint(x: card.minX + 14, y: card.minY + Self.padding + 9), withAttributes: header)
            return
        }
        ("Basic blocks" as NSString).draw(at: NSPoint(x: card.minX + 14, y: card.minY + Self.padding + 9), withAttributes: header)

        let title: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 13.5), .foregroundColor: NSColor.labelColor,
        ]
        let subtitle: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 11.5), .foregroundColor: NSColor.secondaryLabelColor,
        ]
        for row in visibleRows {
            let command = state.items[row]
            let rect = rowRect(row)
            if row == state.selectedIndex || row == hoverRow {
                NSColor.labelColor.withAlphaComponent(row == state.selectedIndex ? 0.08 : 0.05).setFill()
                NSBezierPath(roundedRect: rect, xRadius: 5, yRadius: 5).fill()
            }
            let tile = NSRect(x: rect.minX + 6, y: rect.minY + 6, width: 32, height: 32)
            let tilePath = NSBezierPath(roundedRect: tile.insetBy(dx: 0.5, dy: 0.5), xRadius: 5, yRadius: 5)
            NSColor.textBackgroundColor.setFill()
            tilePath.fill()
            NSColor.separatorColor.setStroke()
            tilePath.stroke()
            drawGlyph(for: command, in: tile)
            (command.title as NSString).draw(at: NSPoint(x: tile.maxX + 10, y: rect.minY + 5), withAttributes: title)
            (command.subtitle as NSString).draw(at: NSPoint(x: tile.maxX + 10, y: rect.minY + 23), withAttributes: subtitle)
        }
    }

    private func drawGlyph(for command: SlashCommand, in tile: NSRect) {
        let text: String?
        switch command {
        case .heading1: text = "H1"
        case .heading2: text = "H2"
        case .heading3: text = "H3"
        default: text = nil
        }
        if let text {
            let attributes: [NSAttributedString.Key: Any] = [
                .font: NSFont.systemFont(ofSize: 13, weight: .semibold), .foregroundColor: NSColor.labelColor,
            ]
            let size = (text as NSString).size(withAttributes: attributes)
            (text as NSString).draw(at: NSPoint(x: tile.midX - size.width / 2, y: tile.midY - size.height / 2),
                                    withAttributes: attributes)
            return
        }
        guard let symbol = NSImage(systemSymbolName: Self.symbolName(for: command), accessibilityDescription: nil),
              let image = symbol.withSymbolConfiguration(.init(pointSize: 14, weight: .regular)) else { return }
        let tinted = NSImage(size: image.size, flipped: false) { rect in
            image.draw(in: rect)
            NSColor.labelColor.set()
            rect.fill(using: .sourceAtop)
            return true
        }
        let size = image.size
        tinted.draw(in: NSRect(x: tile.midX - size.width / 2, y: tile.midY - size.height / 2,
                               width: size.width, height: size.height),
                    from: .zero, operation: .sourceOver, fraction: 1, respectFlipped: true, hints: nil)
    }

    private static func symbolName(for command: SlashCommand) -> String {
        switch command {
        case .bulletList: return "list.bullet"
        case .numberedList: return "list.number"
        case .checklist: return "checklist"
        case .quote: return "text.quote"
        case .codeBlock: return "chevron.left.forwardslash.chevron.right"
        case .divider: return "minus"
        case .drawing: return "square.on.circle"
        case .flashcards: return "rectangle.on.rectangle.angled"
        case .newPage: return "doc.badge.plus"
        case .heading1, .heading2, .heading3: return "textformat.size"
        }
    }

    // MARK: Mouse

    private func row(at point: NSPoint) -> Int? {
        visibleRows.first { rowRect($0).contains(point) }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                                       owner: self))
    }

    override func mouseMoved(with event: NSEvent) {
        let row = row(at: convert(event.locationInWindow, from: nil))
        if row != hoverRow {
            hoverRow = row
            needsDisplay = true
        }
    }

    override func mouseExited(with event: NSEvent) {
        hoverRow = nil
        needsDisplay = true
    }

    override func mouseDown(with event: NSEvent) {
        if let row = row(at: convert(event.locationInWindow, from: nil)) {
            onChoose?(row)
        }
    }

    override func scrollWheel(with event: NSEvent) {
        guard let count = state?.items.count, count > Self.maxVisibleRows else { return }
        let step = event.scrollingDeltaY > 0 ? -1 : event.scrollingDeltaY < 0 ? 1 : 0
        firstVisibleRow = max(0, min(firstVisibleRow + step, count - Self.maxVisibleRows))
        needsDisplay = true
    }
}
