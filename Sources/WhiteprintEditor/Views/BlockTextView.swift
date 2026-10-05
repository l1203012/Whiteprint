import AppKit
import WhiteprintRender

/// What a text block reports back to the editor besides the standard
/// `NSTextViewDelegate` calls.
protocol BlockTextViewDelegate: AnyObject {
    func blockTextViewDidChangeHeight(_ view: BlockTextView)
    func blockTextView(_ view: BlockTextView, toggleCheckboxAt location: Int)
    func blockTextViewDidBecomeFirstResponder(_ view: BlockTextView)
}

/// One run of Markdown text: a transparent, auto-height `NSTextView` with
/// white text, restyled by `MarkdownStyler` as it's edited.
final class BlockTextView: NSTextView, NSTextStorageDelegate {
    static let placeholderText = "Type / for commands"

    let blockID: BlockID
    let palette: BlueprintPalette
    weak var blockDelegate: BlockTextViewDelegate?
    /// Shows the placeholder while empty even when not focused (an empty page).
    var isAlonePlaceholder = false {
        didSet { if oldValue != isAlonePlaceholder { needsDisplay = true } }
    }

    init(blockID: BlockID, text: String, palette: BlueprintPalette, width: CGFloat) {
        self.blockID = blockID
        self.palette = palette
        let storage = NSTextStorage()
        let layout = NSLayoutManager()
        storage.addLayoutManager(layout)
        let container = NSTextContainer(size: NSSize(width: width, height: .greatestFiniteMagnitude))
        container.widthTracksTextView = true
        container.lineFragmentPadding = 0
        layout.addTextContainer(container)
        super.init(frame: NSRect(x: 0, y: 0, width: width, height: HeightEstimate.bodyLineHeight), textContainer: container)

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
        minSize = NSSize(width: 0, height: HeightEstimate.bodyLineHeight)
        maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: .greatestFiniteMagnitude)
        insertionPointColor = palette.text
        selectedTextAttributes = [.backgroundColor: palette.accent.withAlphaComponent(0.32)]
        typingAttributes = TextStyle.base(palette)
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
                                  with: NSAttributedString(string: text, attributes: TextStyle.base(palette)))
        sizeToFit()
    }

    // MARK: Styling

    func textStorage(
        _ textStorage: NSTextStorage, didProcessEditing editedMask: NSTextStorageEditActions,
        range editedRange: NSRange, changeInLength delta: Int
    ) {
        guard editedMask.contains(.editedCharacters) else { return }
        let whole = editedRange.length == textStorage.length
        MarkdownStyler.apply(to: textStorage, in: whole ? nil : editedRange, palette: palette)
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
            needsDisplay = true
            blockDelegate?.blockTextViewDidBecomeFirstResponder(self)
        }
        return accepted
    }

    override func resignFirstResponder() -> Bool {
        let resigned = super.resignFirstResponder()
        if resigned { needsDisplay = true }
        return resigned
    }

    // MARK: Drawing

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        guard string.isEmpty, isAlonePlaceholder || window?.firstResponder === self else { return }
        var attributes = TextStyle.base(palette)
        attributes[.foregroundColor] = palette.muted.withAlphaComponent(0.55)
        (Self.placeholderText as NSString).draw(at: lineRect(at: 0).origin, withAttributes: attributes)
    }
}

/// Base attributes for editor text; `MarkdownStyler` refines them per paragraph.
enum TextStyle {
    static func base(_ palette: BlueprintPalette) -> [NSAttributedString.Key: Any] {
        let paragraph = NSMutableParagraphStyle()
        paragraph.lineHeightMultiple = 1.2
        paragraph.paragraphSpacing = 4
        return [
            .font: NSFont.systemFont(ofSize: MarkdownStyler.defaultFontSize),
            .foregroundColor: palette.text,
            .paragraphStyle: paragraph,
        ]
    }
}
