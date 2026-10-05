import AppKit
import WhiteprintCore
import WhiteprintRender

protocol DeckBlockViewDelegate: AnyObject {
    func deckBlockViewRequestsEditor(_ view: DeckBlockView)
    func deckBlockViewRequestsStudy(_ view: DeckBlockView)
    func deckBlockView(_ view: DeckBlockView, handleKey event: NSEvent) -> Bool
    func deckBlockViewDidChangeHeight(_ view: DeckBlockView)
}

/// A flashcard deck on the page: a header (title, card count, Study and
/// Edit) above the cards as compact rows. Clicking a row reveals or hides
/// its answer; the block is selected (first responder) like a drawing.
final class DeckBlockView: NSView {
    let blockID: BlockID
    weak var delegate: DeckBlockViewDelegate?
    private let palette: BlueprintPalette
    private let studyButton = DeckButton(title: "Study", symbolName: "play.fill")
    private let editButton = DeckButton(title: "Edit", symbolName: nil)
    /// Indices of the cards showing their answers.
    private(set) var revealed: Set<Int> = []
    private var layoutCache: DeckLayout?

    var deck: CardDeck {
        didSet {
            guard deck != oldValue else { return }
            if deck.cards != oldValue.cards { revealed = [] }
            changed()
        }
    }

    var fontSize: CGFloat {
        didSet { if fontSize != oldValue { changed() } }
    }

    init(blockID: BlockID, deck: CardDeck, palette: BlueprintPalette, fontSize: CGFloat = MarkdownStyler.defaultFontSize) {
        self.blockID = blockID
        self.deck = deck
        self.palette = palette
        self.fontSize = fontSize
        super.init(frame: .zero)
        for button in [studyButton, editButton] {
            button.palette = palette
            button.target = self
            addSubview(button)
        }
        studyButton.action = #selector(study)
        editButton.action = #selector(edit)
        studyButton.setAccessibilityLabel("Study flashcards")
        editButton.setAccessibilityLabel("Edit flashcards")
        setAccessibilityRole(.group)
        changed()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    private func changed() {
        layoutCache = nil
        studyButton.isEnabled = !deck.cards.isEmpty
        setAccessibilityLabel("Flashcards: \(DeckLayout.title(of: deck)), \(DeckLayout.countText(deck.cards.count))")
        needsLayout = true
        needsDisplay = true
    }

    /// Height of the block for a column of `width`.
    func height(forWidth width: CGFloat) -> CGFloat {
        layout(forWidth: width).height
    }

    /// Height of a deck with no answers shown, for pages not laid out yet.
    static func height(for deck: CardDeck, width: CGFloat, fontSize: CGFloat) -> CGFloat {
        DeckLayout(deck: deck, width: width, fontSize: fontSize, revealed: []).height
    }

    private func layout(forWidth width: CGFloat) -> DeckLayout {
        if let cached = layoutCache, cached.width == width { return cached }
        let layout = DeckLayout(deck: deck, width: width, fontSize: fontSize, revealed: revealed)
        layoutCache = layout
        return layout
    }

    /// Shows or hides the answer of card `index`.
    func toggleAnswer(_ index: Int) {
        guard deck.cards.indices.contains(index) else { return }
        if revealed.contains(index) { revealed.remove(index) } else { revealed.insert(index) }
        layoutCache = nil
        needsDisplay = true
        delegate?.deckBlockViewDidChangeHeight(self)
    }

    override func layout() {
        super.layout()
        let layout = layout(forWidth: bounds.width)
        let header = layout.header
        let height = (DeckLayout.metrics(fontSize).buttonHeight).rounded()
        var x = header.maxX
        for button in [editButton, studyButton] {
            button.font = .systemFont(ofSize: (fontSize * 0.82).rounded(), weight: .medium)
            let width = button.preferredWidth(height: height)
            x -= width
            button.frame = NSRect(x: x, y: (header.midY - height / 2).rounded(), width: width, height: height)
            x -= 6
        }
    }

    // MARK: Drawing

    override func draw(_ dirtyRect: NSRect) {
        let layout = layout(forWidth: bounds.width)
        let metrics = DeckLayout.metrics(fontSize)
        let selected = window?.firstResponder === self
        let container = NSBezierPath(roundedRect: bounds.insetBy(dx: 0.5, dy: 0.5), xRadius: 8, yRadius: 8)
        palette.text.withAlphaComponent(0.04).setFill()
        container.fill()
        if selected {
            palette.accent.withAlphaComponent(0.1).setFill()
            container.fill()
            palette.accent.setStroke()
            container.lineWidth = 1.5
        } else {
            palette.text.withAlphaComponent(0.14).setStroke()
            container.lineWidth = 1
        }
        container.stroke()

        let title = DeckLayout.headerText(deck, fontSize: fontSize, palette: palette)
        let titleSize = title.size()
        let titleWidth = max(0, studyButton.frame.minX - layout.header.minX - 8)
        title.draw(with: NSRect(x: layout.header.minX, y: layout.header.midY - titleSize.height / 2,
                                width: titleWidth, height: titleSize.height),
                   options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine])

        if deck.cards.isEmpty {
            let empty = NSAttributedString(string: "No cards yet. Click Edit to add some.", attributes: [
                .font: NSFont.systemFont(ofSize: metrics.answerSize), .foregroundColor: palette.muted,
            ])
            empty.draw(at: NSPoint(x: layout.header.minX, y: layout.header.maxY + metrics.rowSpacing))
            return
        }
        for (index, rect) in layout.rows.enumerated() where rect.intersects(dirtyRect) {
            drawRow(index, in: rect, metrics: metrics)
        }
    }

    private func drawRow(_ index: Int, in rect: NSRect, metrics: DeckLayout.Metrics) {
        let card = deck.cards[index]
        let open = revealed.contains(index)
        palette.text.withAlphaComponent(open ? 0.1 : 0.07).setFill()
        NSBezierPath(roundedRect: rect, xRadius: 6, yRadius: 6).fill()
        let content = rect.insetBy(dx: metrics.rowPadding, dy: metrics.rowVerticalPadding)
        let texts = DeckLayout.rowTexts(card, revealed: open, fontSize: fontSize, palette: palette)
        let textWidth = content.width - (open ? 0 : metrics.hintWidth)
        var y = content.minY
        for (i, text) in texts.enumerated() {
            if i > 0 { y += metrics.answerGap }
            let height = DeckLayout.height(of: text, width: textWidth)
            text.draw(with: NSRect(x: content.minX, y: y, width: textWidth, height: height), options: [.usesLineFragmentOrigin])
            y += height
        }
        if !open {
            let hint = NSAttributedString(string: "Show", attributes: [
                .font: NSFont.systemFont(ofSize: metrics.refSize, weight: .medium),
                .foregroundColor: palette.muted.withAlphaComponent(0.7),
            ])
            let size = hint.size()
            hint.draw(at: NSPoint(x: content.maxX - size.width, y: content.minY + (metrics.questionLineHeight - size.height) / 2))
        }
    }

    // MARK: Events

    @objc private func study() {
        delegate?.deckBlockViewRequestsStudy(self)
    }

    @objc private func edit() {
        delegate?.deckBlockViewRequestsEditor(self)
    }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        let point = convert(event.locationInWindow, from: nil)
        let layout = layout(forWidth: bounds.width)
        if let row = layout.rows.firstIndex(where: { $0.contains(point) }) {
            toggleAnswer(row)
        } else if event.clickCount == 2 {
            delegate?.deckBlockViewRequestsEditor(self)
        }
    }

    override func keyDown(with event: NSEvent) {
        if delegate?.deckBlockView(self, handleKey: event) != true {
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

/// Where a deck block's header and card rows go for a column width.
struct DeckLayout {
    struct Metrics {
        var padding: CGFloat
        var headerHeight: CGFloat
        var buttonHeight: CGFloat
        var rowSpacing: CGFloat
        var rowPadding: CGFloat
        var rowVerticalPadding: CGFloat
        var answerGap: CGFloat
        var hintWidth: CGFloat
        var questionSize: CGFloat
        var answerSize: CGFloat
        var refSize: CGFloat
        var questionLineHeight: CGFloat
    }

    static func metrics(_ fontSize: CGFloat) -> Metrics {
        let k = fontSize / MarkdownStyler.defaultFontSize
        let questionSize = (fontSize * 0.95 * 2).rounded() / 2
        return Metrics(
            padding: (12 * k).rounded(), headerHeight: (30 * k).rounded(), buttonHeight: (22 * k).rounded(),
            rowSpacing: (6 * k).rounded(), rowPadding: (12 * k).rounded(), rowVerticalPadding: (8 * k).rounded(),
            answerGap: (3 * k).rounded(), hintWidth: (48 * k).rounded(),
            questionSize: questionSize, answerSize: (fontSize * 0.9 * 2).rounded() / 2, refSize: (fontSize * 0.75 * 2).rounded() / 2,
            questionLineHeight: lineHeight(of: .systemFont(ofSize: questionSize)))
    }

    private static func lineHeight(of font: NSFont) -> CGFloat {
        (font.ascender - font.descender + font.leading).rounded(.up)
    }

    let width: CGFloat
    let header: NSRect
    let rows: [NSRect]
    let height: CGFloat

    init(deck: CardDeck, width: CGFloat, fontSize: CGFloat, revealed: Set<Int>) {
        let m = Self.metrics(fontSize)
        self.width = width
        header = NSRect(x: m.padding, y: m.padding, width: max(0, width - 2 * m.padding), height: m.headerHeight)
        var y = header.maxY + m.rowSpacing
        var rows: [NSRect] = []
        let palette = BlueprintPalette.blueprint
        for (index, card) in deck.cards.enumerated() {
            let open = revealed.contains(index)
            let textWidth = max(20, header.width - 2 * m.rowPadding - (open ? 0 : m.hintWidth))
            let texts = Self.rowTexts(card, revealed: open, fontSize: fontSize, palette: palette)
            let content = texts.map { Self.height(of: $0, width: textWidth) }.reduce(0, +)
                + CGFloat(max(0, texts.count - 1)) * m.answerGap
            let height = (content + 2 * m.rowVerticalPadding).rounded(.up)
            rows.append(NSRect(x: header.minX, y: y, width: header.width, height: height))
            y += height + m.rowSpacing
        }
        if deck.cards.isEmpty {
            y += m.questionLineHeight + m.rowSpacing
        }
        self.rows = rows
        height = (y - m.rowSpacing + m.padding).rounded(.up)
    }

    static func title(of deck: CardDeck) -> String {
        deck.title.flatMap { $0.isEmpty ? nil : $0 } ?? "Flashcards"
    }

    static func countText(_ count: Int) -> String {
        count == 1 ? "1 card" : "\(count) cards"
    }

    /// `🗂 Title · N cards`, the count muted.
    static func headerText(_ deck: CardDeck, fontSize: CGFloat, palette: BlueprintPalette) -> NSAttributedString {
        let text = NSMutableAttributedString(string: "🗂  " + title(of: deck), attributes: [
            .font: NSFont.systemFont(ofSize: fontSize, weight: .semibold), .foregroundColor: palette.text,
        ])
        text.append(NSAttributedString(string: "  ·  " + countText(deck.cards.count), attributes: [
            .font: NSFont.systemFont(ofSize: (fontSize * 0.87).rounded()), .foregroundColor: palette.muted,
        ]))
        let paragraph = NSMutableParagraphStyle()
        paragraph.lineBreakMode = .byTruncatingTail
        text.addAttribute(.paragraphStyle, value: paragraph, range: NSRange(location: 0, length: text.length))
        return text
    }

    /// The question, and with the answer revealed, the answer and source.
    static func rowTexts(_ card: Flashcard, revealed: Bool, fontSize: CGFloat, palette: BlueprintPalette) -> [NSAttributedString] {
        let m = metrics(fontSize)
        var texts = [NSAttributedString(string: card.question.isEmpty ? " " : card.question, attributes: [
            .font: NSFont.systemFont(ofSize: m.questionSize, weight: .medium), .foregroundColor: palette.text,
        ])]
        guard revealed else { return texts }
        texts.append(NSAttributedString(string: card.answer.isEmpty ? "No answer" : card.answer, attributes: [
            .font: NSFont.systemFont(ofSize: m.answerSize), .foregroundColor: palette.muted,
        ]))
        if let ref = card.ref, !ref.isEmpty {
            texts.append(NSAttributedString(string: ref, attributes: [
                .font: NSFont.systemFont(ofSize: m.refSize), .foregroundColor: palette.muted.withAlphaComponent(0.75),
            ]))
        }
        return texts
    }

    static func height(of text: NSAttributedString, width: CGFloat) -> CGFloat {
        text.boundingRect(with: NSSize(width: width, height: .greatestFiniteMagnitude),
                          options: [.usesLineFragmentOrigin, .usesFontLeading]).height.rounded(.up)
    }
}

/// A small pill button for the deck header, drawn for the blueprint page.
final class DeckButton: NSButton {
    var palette = BlueprintPalette.blueprint
    private let symbolName: String?
    private var isHovered = false {
        didSet { needsDisplay = true }
    }

    init(title: String, symbolName: String?) {
        self.symbolName = symbolName
        super.init(frame: .zero)
        self.title = title
        isBordered = false
        setButtonType(.momentaryChange)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    private var symbol: NSImage? {
        guard let symbolName, let font else { return nil }
        return NSImage(systemSymbolName: symbolName, accessibilityDescription: nil)?
            .withSymbolConfiguration(.init(pointSize: font.pointSize * 0.75, weight: .semibold))
    }

    func preferredWidth(height: CGFloat) -> CGFloat {
        let titleWidth = (title as NSString).size(withAttributes: [.font: font ?? .systemFont(ofSize: 12)]).width
        let symbolWidth = symbol.map { $0.size.width + 5 } ?? 0
        return (titleWidth + symbolWidth + height * 0.9).rounded(.up)
    }

    override func draw(_ dirtyRect: NSRect) {
        let pressed = isHighlighted
        let alpha: CGFloat = !isEnabled ? 0.06 : pressed ? 0.28 : isHovered ? 0.2 : 0.13
        palette.text.withAlphaComponent(alpha).setFill()
        NSBezierPath(roundedRect: bounds, xRadius: bounds.height / 2, yRadius: bounds.height / 2).fill()
        let color = isEnabled ? palette.text : palette.muted.withAlphaComponent(0.5)
        let attributes: [NSAttributedString.Key: Any] = [.font: font ?? .systemFont(ofSize: 12), .foregroundColor: color]
        let titleSize = (title as NSString).size(withAttributes: attributes)
        let image = symbol
        let total = titleSize.width + (image.map { $0.size.width + 5 } ?? 0)
        var x = ((bounds.width - total) / 2).rounded()
        if let image {
            let tinted = NSImage(size: image.size, flipped: false) { rect in
                image.draw(in: rect)
                color.set()
                rect.fill(using: .sourceAtop)
                return true
            }
            tinted.draw(in: NSRect(x: x, y: ((bounds.height - image.size.height) / 2).rounded(),
                                   width: image.size.width, height: image.size.height),
                        from: .zero, operation: .sourceOver, fraction: 1, respectFlipped: true, hints: nil)
            x += image.size.width + 5
        }
        (title as NSString).draw(at: NSPoint(x: x, y: ((bounds.height - titleSize.height) / 2).rounded()), withAttributes: attributes)
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                                       owner: self))
    }

    override func mouseEntered(with event: NSEvent) { isHovered = true }
    override func mouseExited(with event: NSEvent) { isHovered = false }
}
