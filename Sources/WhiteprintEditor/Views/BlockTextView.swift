import AppKit
import WhiteprintRender

/// What a text block reports back to the editor besides the standard
/// `NSTextViewDelegate` calls.
protocol BlockTextViewDelegate: AnyObject {
    func blockTextViewDidChangeHeight(_ view: BlockTextView)
    func blockTextView(_ view: BlockTextView, toggleCheckboxAt location: Int)
    func blockTextViewDidBecomeFirstResponder(_ view: BlockTextView)
    func blockTextViewTouchBar(_ view: BlockTextView) -> NSTouchBar?
}

/// One run of Markdown text: a transparent, auto-height `NSTextView` with
/// white text, restyled by `MarkdownStyler` as it's edited.
///
/// With `concealsMarkup`, the markup of every paragraph but the ones holding
/// the caret or selection is hidden (see `MarkdownLayoutManager`).
final class BlockTextView: NSTextView, NSTextStorageDelegate {
    static let placeholderText = "Type / for commands"

    let blockID: BlockID
    let palette: BlueprintPalette
    weak var blockDelegate: BlockTextViewDelegate?
    /// Shows the placeholder while empty even when not focused (an empty page).
    var isAlonePlaceholder = false {
        didSet { if oldValue != isAlonePlaceholder { needsDisplay = true } }
    }
    var fontSize: CGFloat {
        didSet {
            guard oldValue != fontSize else { return }
            typingAttributes = TextStyle.base(palette, fontSize: fontSize)
            minSize = NSSize(width: 0, height: TextStyle.lineHeight(fontSize))
            restyle()
        }
    }
    var concealsMarkup: Bool {
        didSet {
            guard oldValue != concealsMarkup else { return }
            revealedRange = revealedParagraphs
            restyle()
        }
    }
    /// The paragraphs whose markup shows while concealing, as last styled.
    private(set) var revealedRange: NSRange?
    private var hasFocus = false

    init(blockID: BlockID, text: String, palette: BlueprintPalette, width: CGFloat,
         fontSize: CGFloat = MarkdownStyler.defaultFontSize, concealsMarkup: Bool = false) {
        self.blockID = blockID
        self.palette = palette
        self.fontSize = fontSize
        self.concealsMarkup = concealsMarkup
        let storage = NSTextStorage()
        let layout = MarkdownLayoutManager()
        layout.palette = palette
        storage.addLayoutManager(layout)
        let container = NSTextContainer(size: NSSize(width: width, height: .greatestFiniteMagnitude))
        container.widthTracksTextView = true
        container.lineFragmentPadding = 0
        layout.addTextContainer(container)
        super.init(frame: NSRect(x: 0, y: 0, width: width, height: TextStyle.lineHeight(fontSize)), textContainer: container)

        storage.delegate = self
        drawsBackground = false
        isRichText = false
        importsGraphics = false
        allowsUndo = true
        usesFontPanel = false
        isAutomaticQuoteSubstitutionEnabled = false
        isAutomaticDashSubstitutionEnabled = false
        isAutomaticTextReplacementEnabled = false
        isAutomaticSpellingCorrectionEnabled = false
        isAutomaticLinkDetectionEnabled = false
        smartInsertDeleteEnabled = false
        textContainerInset = .zero
        isVerticallyResizable = true
        isHorizontallyResizable = false
        minSize = NSSize(width: 0, height: TextStyle.lineHeight(fontSize))
        maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: .greatestFiniteMagnitude)
        insertionPointColor = palette.text
        selectedTextAttributes = [.backgroundColor: palette.accent.withAlphaComponent(0.32)]
        typingAttributes = TextStyle.base(palette, fontSize: fontSize)
        setAccessibilityLabel("Text block")
        setText(text)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    /// Replaces the whole text without registering undo or notifying the
    /// delegate (for model → view syncs).
    func setText(_ text: String) {
        guard let storage = textStorage, storage.string != text else { return }
        storage.replaceCharacters(in: NSRange(location: 0, length: storage.length),
                                  with: NSAttributedString(string: text, attributes: TextStyle.base(palette, fontSize: fontSize)))
        sizeToFit()
    }

    // MARK: Styling

    func textStorage(
        _ textStorage: NSTextStorage, didProcessEditing editedMask: NSTextStorageEditActions,
        range editedRange: NSRange, changeInLength delta: Int
    ) {
        guard editedMask.contains(.editedCharacters) else { return }
        if let revealed = revealedRange {
            revealedRange = Self.range(revealed, afterEditing: editedRange, delta: delta, in: textStorage.string as NSString)
        }
        let whole = editedRange.length == textStorage.length
        style(textStorage, in: whole ? nil : editedRange)
    }

    private func style(_ storage: NSTextStorage, in range: NSRange?) {
        MarkdownStyler.apply(to: storage, in: range, palette: palette, fontSize: fontSize,
                             concealsMarkup: concealsMarkup, revealing: revealedRange)
    }

    /// Restyles everything, e.g. after the font size or concealing changed.
    private func restyle() {
        guard let storage = textStorage, storage.length > 0 else { return }
        style(storage, in: nil)
        sizeToFit()
    }

    /// The paragraphs that should show their markup now: the caret's or the
    /// selection's, while focused and concealing.
    private var revealedParagraphs: NSRange? {
        guard concealsMarkup, hasFocus else { return nil }
        return (string as NSString).paragraphRange(for: selectedRange())
    }

    /// Reveals the markup of the caret's paragraphs and conceals it again in
    /// the ones the caret left.
    private func updateRevealedParagraphs() {
        let revealed = revealedParagraphs
        guard revealed != revealedRange, let storage = textStorage else { return }
        let previous = revealedRange
        revealedRange = revealed
        for range in [previous, revealed].compactMap({ $0 }) {
            let location = min(range.location, storage.length)
            style(storage, in: NSRange(location: location, length: min(range.length, storage.length - location)))
        }
        sizeToFit()
    }

    /// Where `range` is after an edit that left `edited` (in the new text)
    /// and changed the length by `delta`: moved along, untouched, or grown to
    /// the paragraphs of both when they meet.
    static func range(_ range: NSRange, afterEditing edited: NSRange, delta: Int, in text: NSString) -> NSRange {
        let oldEditEnd = NSMaxRange(edited) - delta
        if oldEditEnd < range.location {
            return NSRange(location: range.location + delta, length: range.length)
        }
        if edited.location > NSMaxRange(range) {
            return range
        }
        let start = min(range.location, edited.location, text.length)
        let end = min(text.length, max(NSMaxRange(range) + delta, NSMaxRange(edited)))
        return text.paragraphRange(for: NSRange(location: start, length: max(0, end - start)))
    }

    override func setSelectedRanges(_ ranges: [NSValue], affinity: NSSelectionAffinity, stillSelecting: Bool) {
        super.setSelectedRanges(ranges, affinity: affinity, stillSelecting: stillSelecting)
        if !stillSelecting { updateRevealedParagraphs() }
    }

    // MARK: Geometry

    override func setFrameSize(_ newSize: NSSize) {
        let changed = newSize.height != frame.height
        super.setFrameSize(newSize)
        if changed { blockDelegate?.blockTextViewDidChangeHeight(self) }
    }

    /// The line fragment the caret at `index` sits on, in view coordinates.
    func lineRect(at index: Int) -> NSRect {
        guard let layout = layoutManager, let container = textContainer, let storage = textStorage else { return .zero }
        layout.ensureLayout(for: container)
        let origin = textContainerOrigin
        if storage.length == 0 || (index >= storage.length && layout.extraLineFragmentTextContainer != nil) {
            let extra = layout.extraLineFragmentRect
            return extra.offsetBy(dx: origin.x, dy: origin.y)
        }
        let glyph = layout.glyphIndexForCharacter(at: min(index, storage.length - 1))
        return layout.lineFragmentRect(forGlyphAt: glyph, effectiveRange: nil).offsetBy(dx: origin.x, dy: origin.y)
    }

    /// The rect covering a character range, in view coordinates.
    func rect(for range: NSRange) -> NSRect {
        guard let layout = layoutManager, let container = textContainer else { return .zero }
        layout.ensureLayout(for: container)
        let glyphs = layout.glyphRange(forCharacterRange: range, actualCharacterRange: nil)
        let origin = textContainerOrigin
        return layout.boundingRect(forGlyphRange: glyphs, in: container).offsetBy(dx: origin.x, dy: origin.y)
    }

    var caretIsOnFirstLine: Bool {
        lineRect(at: selectedRange().location).minY <= lineRect(at: 0).minY + 0.5
    }

    var caretIsOnLastLine: Bool {
        let length = textStorage?.length ?? 0
        return lineRect(at: NSMaxRange(selectedRange())).maxY >= lineRect(at: length).maxY - 0.5
    }

    /// The caret's horizontal position, kept when moving between blocks.
    var caretX: CGFloat {
        let index = selectedRange().location
        let length = textStorage?.length ?? 0
        let ns = string as NSString
        if index < length, ns.character(at: index) != 0x0A {
            return rect(for: NSRange(location: index, length: 1)).minX
        }
        if index > 0, ns.character(at: index - 1) != 0x0A {
            return rect(for: NSRange(location: index - 1, length: 1)).maxX
        }
        return 0
    }

    /// Puts the caret on the first or last line, as near to `x` as possible.
    func placeCaret(atX x: CGFloat, onFirstLine first: Bool) {
        let line = lineRect(at: first ? 0 : (textStorage?.length ?? 0))
        let index = characterIndexForInsertion(at: NSPoint(x: x, y: line.midY))
        setSelectedRange(NSRange(location: min(index, textStorage?.length ?? 0), length: 0))
    }

    // MARK: Events

    override func mouseDown(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        if let box = checkbox(at: point) {
            blockDelegate?.blockTextView(self, toggleCheckboxAt: box.location)
            return
        }
        super.mouseDown(with: event)
    }

    /// The checkbox under `point`, if any.
    func checkbox(at point: NSPoint) -> NSRange? {
        let index = characterIndexForInsertion(at: point)
        guard let box = MarkdownEditing.checkboxRange(at: index, in: string) else { return nil }
        return rect(for: box).insetBy(dx: -4, dy: -3).contains(point) ? box : nil
    }

    override func becomeFirstResponder() -> Bool {
        let accepted = super.becomeFirstResponder()
        if accepted {
            hasFocus = true
            needsDisplay = true
            updateRevealedParagraphs()
            blockDelegate?.blockTextViewDidBecomeFirstResponder(self)
        }
        return accepted
    }

    override func resignFirstResponder() -> Bool {
        let resigned = super.resignFirstResponder()
        if resigned {
            hasFocus = false
            needsDisplay = true
            updateRevealedParagraphs()
        }
        return resigned
    }

    override func makeTouchBar() -> NSTouchBar? {
        blockDelegate?.blockTextViewTouchBar(self) ?? super.makeTouchBar()
    }

    // MARK: Drawing

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        guard string.isEmpty, isAlonePlaceholder || window?.firstResponder === self else { return }
        var attributes = TextStyle.base(palette, fontSize: fontSize)
        attributes[.foregroundColor] = palette.muted.withAlphaComponent(0.55)
        (Self.placeholderText as NSString).draw(at: lineRect(at: 0).origin, withAttributes: attributes)
    }
}

/// Base attributes for editor text; `MarkdownStyler` refines them per paragraph.
enum TextStyle {
    static func base(_ palette: BlueprintPalette, fontSize: CGFloat = MarkdownStyler.defaultFontSize) -> [NSAttributedString.Key: Any] {
        let paragraph = NSMutableParagraphStyle()
        paragraph.lineHeightMultiple = 1.2
        paragraph.paragraphSpacing = 4
        return [
            .font: NSFont.systemFont(ofSize: fontSize),
            .foregroundColor: palette.text,
            .paragraphStyle: paragraph,
        ]
    }

    /// The minimum height of a line of body text.
    static func lineHeight(_ fontSize: CGFloat) -> CGFloat {
        (HeightEstimate.bodyLineHeight * fontSize / MarkdownStyler.defaultFontSize).rounded()
    }
}
