import AppKit
import WhiteprintCore

/// The deck popover's content: the title and a list of cards with question,
/// answer and source fields, which can be added, removed and reordered.
/// Done (⌘↩) commits; so does any other close (clicking outside, esc).
final class DeckEditor: NSViewController, NSTextFieldDelegate, NSPopoverDelegate {
    static let width: CGFloat = 440

    let blockID: BlockID
    private(set) var draft: DeckDraft
    private let onCommit: (CardDeck) -> Void
    private let titleField = NSTextField()
    private let cardStack = NSStackView()
    private let countLabel = NSTextField(labelWithString: "")
    private var committed = false
    weak var popover: NSPopover?

    init(blockID: BlockID, deck: CardDeck, onCommit: @escaping (CardDeck) -> Void) {
        self.blockID = blockID
        draft = DeckDraft(deck)
        self.onCommit = onCommit
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override func loadView() {
        let root = NSView(frame: NSRect(x: 0, y: 0, width: Self.width, height: 460))

        let heading = NSTextField(labelWithString: "Flashcards")
        heading.font = .systemFont(ofSize: 13, weight: .semibold)
        countLabel.font = .systemFont(ofSize: 11)
        countLabel.textColor = .secondaryLabelColor

        titleField.placeholderString = "Deck title"
        titleField.stringValue = draft.title
        titleField.font = .systemFont(ofSize: 13)
        titleField.delegate = self
        titleField.setAccessibilityLabel("Deck title")

        cardStack.orientation = .vertical
        cardStack.alignment = .leading
        cardStack.spacing = 10
        cardStack.edgeInsets = NSEdgeInsets(top: 8, left: 8, bottom: 8, right: 8)
        cardStack.translatesAutoresizingMaskIntoConstraints = false
        let documentView = FlippedView()
        documentView.translatesAutoresizingMaskIntoConstraints = false
        documentView.addSubview(cardStack)
        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.borderType = .bezelBorder
        scroll.drawsBackground = true
        scroll.backgroundColor = .textBackgroundColor
        scroll.documentView = documentView
        scroll.translatesAutoresizingMaskIntoConstraints = false

        let add = NSButton(title: "Add card", image: NSImage(systemSymbolName: "plus", accessibilityDescription: nil)!,
                           target: self, action: #selector(addCard))
        add.bezelStyle = .rounded
        add.imagePosition = .imageLeading
        let done = NSButton(title: "Done", target: self, action: #selector(done))
        done.bezelStyle = .rounded
        done.keyEquivalent = "\r"
        done.keyEquivalentModifierMask = .command
        let hint = NSTextField(labelWithString: "⌘↩")
        hint.font = .systemFont(ofSize: 11)
        hint.textColor = .tertiaryLabelColor

        let header = NSStackView(views: [heading, countLabel, NSView()])
        let footer = NSStackView(views: [add, NSView(), hint, done])
        let stack = NSStackView(views: [header, titleField, scroll, footer])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)
        stack.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(stack)
        let clip = scroll.contentView
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            stack.topAnchor.constraint(equalTo: root.topAnchor),
            stack.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            header.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            titleField.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            scroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            scroll.heightAnchor.constraint(equalToConstant: 320),
            footer.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            documentView.leadingAnchor.constraint(equalTo: clip.leadingAnchor),
            documentView.trailingAnchor.constraint(equalTo: clip.trailingAnchor),
            documentView.topAnchor.constraint(equalTo: clip.topAnchor),
            cardStack.leadingAnchor.constraint(equalTo: documentView.leadingAnchor),
            cardStack.trailingAnchor.constraint(equalTo: documentView.trailingAnchor),
            cardStack.topAnchor.constraint(equalTo: documentView.topAnchor),
            cardStack.bottomAnchor.constraint(equalTo: documentView.bottomAnchor),
        ])
        view = root
        rebuildCards()
    }

    override func viewDidAppear() {
        super.viewDidAppear()
        let target = draft.cards.isEmpty ? titleField : cardRows.first?.question ?? titleField
        view.window?.makeFirstResponder(target)
    }

    // MARK: Cards

    private var cardRows: [CardEditorRow] {
        cardStack.arrangedSubviews.compactMap { $0 as? CardEditorRow }
    }

    private func rebuildCards() {
        for view in cardStack.arrangedSubviews {
            cardStack.removeArrangedSubview(view)
            view.removeFromSuperview()
        }
        for (index, card) in draft.cards.enumerated() {
            let row = CardEditorRow(index: index, card: card, count: draft.cards.count)
            row.question.delegate = self
            row.answer.delegate = self
            row.ref.delegate = self
            row.onMove = { [weak self] index, delta in self?.moveCard(index, by: delta) }
            row.onRemove = { [weak self] index in self?.removeCard(index) }
            cardStack.addArrangedSubview(row)
            row.widthAnchor.constraint(equalTo: cardStack.widthAnchor, constant: -16).isActive = true
        }
        if draft.cards.isEmpty {
            let empty = NSTextField(labelWithString: "No cards yet.")
            empty.textColor = .secondaryLabelColor
            cardStack.addArrangedSubview(empty)
        }
        countLabel.stringValue = DeckLayout.countText(draft.cards.count)
    }

    @objc func addCard() {
        let index = draft.addCard()
        rebuildCards()
        let row = cardRows[index]
        view.window?.makeFirstResponder(row.question)
        row.scrollToVisible(row.bounds)
    }

    func removeCard(_ index: Int) {
        draft.removeCard(at: index)
        rebuildCards()
    }

    func moveCard(_ index: Int, by delta: Int) {
        guard draft.moveCard(at: index, by: delta) else { return }
        rebuildCards()
    }

    func controlTextDidChange(_ notification: Notification) {
        guard let field = notification.object as? NSTextField else { return }
        if field === titleField {
            draft.title = field.stringValue
            return
        }
        guard let row = cardRows.first(where: { [$0.question, $0.answer, $0.ref].contains(field) }),
              draft.cards.indices.contains(row.index) else { return }
        switch field {
        case row.question: draft.cards[row.index].question = field.stringValue
        case row.answer: draft.cards[row.index].answer = field.stringValue
        default: draft.cards[row.index].ref = field.stringValue
        }
        field.invalidateIntrinsicContentSize()
    }

    /// ⌥↩ inserts a line break in a question or answer.
    func control(_ control: NSControl, textView: NSTextView, doCommandBy selector: Selector) -> Bool {
        guard selector == #selector(NSResponder.insertNewlineIgnoringFieldEditor(_:)), control !== titleField else { return false }
        textView.insertText("\n", replacementRange: textView.selectedRange())
        return true
    }

    // MARK: Committing

    @objc private func done() {
        close()
    }

    /// Closes the popover (which commits) or commits directly when not shown.
    func close() {
        view.window?.makeFirstResponder(nil)
        if let popover, popover.isShown {
            popover.performClose(nil)
        } else {
            commit()
        }
    }

    /// Closes without committing (the deck went away).
    func discard() {
        committed = true
        popover?.close()
    }

    func commit() {
        guard !committed else { return }
        committed = true
        onCommit(draft.deck)
    }

    func popoverDidClose(_ notification: Notification) {
        commit()
    }
}

/// One card in the deck editor: number, question, answer and source fields,
/// and buttons to move it up or down or remove it.
private final class CardEditorRow: NSView {
    let index: Int
    let question = CardEditorRow.field(placeholder: "Question", size: 13, weight: .medium)
    let answer = CardEditorRow.field(placeholder: "Answer", size: 13, weight: .regular)
    let ref = CardEditorRow.field(placeholder: "Source (optional), e.g. Lecture 3 · slide 14", size: 11, weight: .regular)
    var onMove: ((Int, Int) -> Void)?
    var onRemove: ((Int) -> Void)?

    init(index: Int, card: Flashcard, count: Int) {
        self.index = index
        super.init(frame: .zero)
        question.stringValue = card.question
        answer.stringValue = card.answer
        ref.stringValue = card.ref ?? ""
        question.setAccessibilityLabel("Question \(index + 1)")
        answer.setAccessibilityLabel("Answer \(index + 1)")
        ref.setAccessibilityLabel("Source \(index + 1)")

        let number = NSTextField(labelWithString: "\(index + 1)")
        number.font = .monospacedDigitSystemFont(ofSize: 11, weight: .semibold)
        number.textColor = .secondaryLabelColor
        let up = Self.button("chevron.up", "Move card up", #selector(moveCardUp))
        let down = Self.button("chevron.down", "Move card down", #selector(moveCardDown))
        let remove = Self.button("trash", "Remove card", #selector(removeCard))
        up.isEnabled = index > 0
        down.isEnabled = index < count - 1
        for button in [up, down, remove] { button.target = self }

        let fields = NSStackView(views: [question, answer, ref])
        fields.orientation = .vertical
        fields.alignment = .leading
        fields.spacing = 4
        let buttons = NSStackView(views: [up, down, remove])
        buttons.spacing = 2
        let row = NSStackView(views: [number, fields, buttons])
        row.alignment = NSLayoutConstraint.Attribute.top
        row.spacing = 8
        row.translatesAutoresizingMaskIntoConstraints = false
        addSubview(row)
        NSLayoutConstraint.activate([
            row.leadingAnchor.constraint(equalTo: leadingAnchor),
            row.trailingAnchor.constraint(equalTo: trailingAnchor),
            row.topAnchor.constraint(equalTo: topAnchor),
            row.bottomAnchor.constraint(equalTo: bottomAnchor),
            number.widthAnchor.constraint(equalToConstant: 18),
            question.widthAnchor.constraint(equalTo: fields.widthAnchor),
            answer.widthAnchor.constraint(equalTo: fields.widthAnchor),
            ref.widthAnchor.constraint(equalTo: fields.widthAnchor),
        ])
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    private static func field(placeholder: String, size: CGFloat, weight: NSFont.Weight) -> NSTextField {
        let field = NSTextField()
        field.placeholderString = placeholder
        field.font = .systemFont(ofSize: size, weight: weight)
        field.usesSingleLineMode = false
        field.cell?.wraps = true
        field.cell?.isScrollable = false
        field.lineBreakMode = .byWordWrapping
        field.preferredMaxLayoutWidth = 300
        return field
    }

    private static func button(_ symbol: String, _ label: String, _ action: Selector) -> NSButton {
        let button = NSButton(image: NSImage(systemSymbolName: symbol, accessibilityDescription: label)!, target: nil, action: action)
        button.isBordered = false
        button.toolTip = label
        button.setAccessibilityLabel(label)
        button.widthAnchor.constraint(equalToConstant: 20).isActive = true
        return button
    }

    @objc private func moveCardUp() { onMove?(index, -1) }
    @objc private func moveCardDown() { onMove?(index, 1) }
    @objc private func removeCard() { onRemove?(index) }
}

private final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}
