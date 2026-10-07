import AppKit
import WhiteprintCore
import WhiteprintRender

/// "Study Flashcards": one card at a time on blueprint paper, question
/// first. Space or a click flips it; Again / Hard / Good / Easy (keys 1–4)
/// schedule it and move on. Ends on a summary of the run.
final class FlashcardStudyWindowController: NSWindowController, NSWindowDelegate {
    private static var shared: FlashcardStudyWindowController?

    private let store: FlashcardProgressStore
    private var noteURL: URL
    private var deck: CardDeck
    private var noteTitle: String
    private var session: FlashcardSession
    private var shuffled = false

    private let titleLabel = NSTextField(labelWithString: "")
    private let summaryLabel = NSTextField(labelWithString: "")
    private let shuffleBox = NSButton(checkboxWithTitle: "Shuffle", target: nil, action: nil)
    private let card = FlashcardView(palette: ViewPreferences.shared.pageTheme.palette)
    private let hint = NSTextField(labelWithString: "")
    private let flipButton = NSButton(title: "Show Answer", target: nil, action: nil)
    private let ratingRow = NSStackView()
    private var ratingButtons: [NSButton] = []
    private let finishRow = NSStackView()
    private let studyAllButton = NSButton(title: "Study All Cards", target: nil, action: nil)

    /// Opens (or reuses) the study window on deck `deckID` of the note at `note`.
    static func show(note: URL, deckID: String) {
        let loaded: Note
        do {
            loaded = try NoteDocuments.document(for: note)?.note ?? NotesLibrary.read(note)
        } catch {
            NSApp.presentError(error)
            return
        }
        guard let deck = loaded.deck(deckID)?.deck, !deck.cards.isEmpty else {
            let alert = NSAlert()
            alert.messageText = "This deck has no cards yet."
            alert.informativeText = "Add questions and answers to the deck, then study it."
            alert.runModal()
            return
        }
        let title = NoteTitle.display(for: loaded, fileURL: note)
        if let controller = shared {
            controller.load(note: note, noteTitle: title, deck: deck)
            controller.showWindow(nil)
        } else {
            let controller = FlashcardStudyWindowController(note: note, noteTitle: title, deck: deck, store: AppServices.shared.flashcards)
            shared = controller
            controller.showWindow(nil)
        }
        shared?.window?.makeKeyAndOrderFront(nil)
    }

    init(note: URL, noteTitle: String, deck: CardDeck, store: FlashcardProgressStore) {
        self.store = store
        noteURL = note
        self.deck = deck
        self.noteTitle = noteTitle
        session = FlashcardSession(deck: deck, progress: store.progress(note: note, deck: deck.id), now: Date())
        let window = StudyWindow(
            contentRect: NSRect(x: 0, y: 0, width: 680, height: 560),
            styleMask: [.titled, .closable, .resizable, .miniaturizable],
            backing: .buffered, defer: true
        )
        window.title = "Study Flashcards"
        window.minSize = NSSize(width: 480, height: 440)
        window.isReleasedWhenClosed = false
        window.tabbingMode = .disallowed
        super.init(window: window)
        window.delegate = self
        window.onKey = { [weak self] key in self?.handleKey(key) ?? false }
        window.contentView = makeContent()
        window.center()
        refresh()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    private func load(note: URL, noteTitle: String, deck: CardDeck) {
        noteURL = note
        self.noteTitle = noteTitle
        self.deck = deck
        restart(everything: false)
    }

    private func restart(everything: Bool) {
        session = FlashcardSession(
            deck: deck, progress: store.progress(note: noteURL, deck: deck.id), now: Date(),
            shuffled: shuffled, everything: everything
        )
        refresh()
    }

    func windowWillClose(_ notification: Notification) {
        Self.shared = nil
    }

    // MARK: Layout

    private func makeContent() -> NSView {
        titleLabel.font = .systemFont(ofSize: 17, weight: .semibold)
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        summaryLabel.textColor = .secondaryLabelColor
        summaryLabel.font = .monospacedDigitSystemFont(ofSize: 12, weight: .regular)
        shuffleBox.target = self
        shuffleBox.action = #selector(toggleShuffle(_:))
        shuffleBox.refusesFirstResponder = true
        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let header = NSStackView(views: [titleLabel, summaryLabel, spacer, shuffleBox])
        header.spacing = 10
        header.alignment = .firstBaseline

        card.onClick = { [weak self] in self?.flip() }
        card.setContentHuggingPriority(.init(1), for: .vertical)

        hint.textColor = .tertiaryLabelColor
        hint.font = .systemFont(ofSize: 11)
        hint.alignment = .center

        flipButton.target = self
        flipButton.action = #selector(flip)
        flipButton.bezelStyle = .rounded
        flipButton.controlSize = .large
        flipButton.refusesFirstResponder = true

        ratingButtons = CardRating.allCases.map { rating in
            let button = NSButton(title: rating.title, target: self, action: #selector(rateButtonPressed(_:)))
            button.tag = rating.rawValue
            button.bezelStyle = .rounded
            button.controlSize = .large
            button.refusesFirstResponder = true
            return button
        }
        ratingRow.setViews(ratingButtons, in: .center)
        ratingRow.spacing = 10
        ratingRow.distribution = .fillEqually

        studyAllButton.target = self
        studyAllButton.action = #selector(studyAll(_:))
        studyAllButton.bezelStyle = .rounded
        studyAllButton.controlSize = .large
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow(_:)))
        close.bezelStyle = .rounded
        close.controlSize = .large
        finishRow.setViews([studyAllButton, close], in: .center)
        finishRow.spacing = 10

        let controls = NSStackView(views: [flipButton, ratingRow, finishRow])
        controls.orientation = .vertical

        let stack = NSStackView(views: [header, card, controls, hint])
        stack.orientation = .vertical
        stack.alignment = .centerX
        stack.spacing = 14
        stack.edgeInsets = NSEdgeInsets(top: 18, left: 24, bottom: 16, right: 24)
        for view in [header, card] {
            view.translatesAutoresizingMaskIntoConstraints = false
            view.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -48).isActive = true
        }
        card.heightAnchor.constraint(greaterThanOrEqualToConstant: 260).isActive = true
        return stack
    }

    private func refresh() {
        titleLabel.stringValue = deck.title.flatMap { $0.isEmpty ? nil : $0 } ?? noteTitle
        let total = FlashcardSession.summary(of: deck, progress: session.progress, now: Date())
        let left = session.queue.count
        summaryLabel.stringValue = session.isFinished ? total.description : "\(total) · \(left) left"
        shuffleBox.state = shuffled ? .on : .off

        if let item = session.current {
            card.content = .card(item.card, flipped: session.isFlipped)
            hint.stringValue = session.isFlipped ? "Rate with 1 – 4 · Space to flip back" : "Space or click to flip"
            let now = Date()
            for button in ratingButtons {
                guard let rating = CardRating(rawValue: button.tag) else { continue }
                let label = FlashcardScheduler.intervalLabel(after: session.progress[item.key], rating: rating, now: now)
                button.title = "\(rating.title)  ·  \(label)"
                button.toolTip = "\(rating.rawValue): back in \(label)"
            }
        } else if session.stats.answers == 0 {
            card.content = .message("Nothing due right now", "Every card in this deck is scheduled for later. Study them all anyway, or come back later.")
            hint.stringValue = ""
        } else {
            card.content = .finished(session.stats)
            hint.stringValue = ""
        }
        flipButton.isHidden = session.current == nil || session.isFlipped
        ratingRow.isHidden = session.current == nil || !session.isFlipped
        finishRow.isHidden = session.current != nil
        studyAllButton.title = session.stats.answers == 0 ? "Study All Cards" : "Study Again"
    }

    // MARK: Actions

    @objc private func flip() {
        session.flip()
        refresh()
    }

    @objc private func rateButtonPressed(_ sender: NSButton) {
        CardRating(rawValue: sender.tag).map(rate)
    }

    private func rate(_ rating: CardRating) {
        guard session.isFlipped, let (key, progress) = session.rate(rating, now: Date()) else { return }
        do {
            try store.save(progress, card: key, note: noteURL, deck: deck.id)
        } catch {
            NSLog("Whiteprint: couldn't save flashcard progress: \(error)")
        }
        NotificationCenter.default.post(name: .flashcardProgressDidChange, object: nil)
        refresh()
    }

    @objc private func toggleShuffle(_ sender: NSButton) {
        shuffled = sender.state == .on
        if session.stats.answers == 0 { restart(everything: false) }
    }

    @objc private func studyAll(_ sender: Any?) {
        restart(everything: true)
    }

    @objc private func closeWindow(_ sender: Any?) {
        window?.close()
    }

    /// Space / Return flip, 1–4 rate, Escape closes.
    private func handleKey(_ event: NSEvent) -> Bool {
        guard event.modifierFlags.intersection([.command, .control, .option]).isEmpty else { return false }
        switch event.charactersIgnoringModifiers {
        case " ", "\r":
            guard session.current != nil else { return false }
            flip()
        case "1", "2", "3", "4":
            guard let digit = event.charactersIgnoringModifiers.flatMap(Int.init), let rating = CardRating(rawValue: digit) else { return false }
            rate(rating)
        case "\u{1b}":
            window?.close()
        default:
            return false
        }
        return true
    }
}

/// Routes plain key presses to the study controller before anything else.
private final class StudyWindow: NSWindow {
    var onKey: ((NSEvent) -> Bool)?

    override func keyDown(with event: NSEvent) {
        if onKey?(event) != true { super.keyDown(with: event) }
    }

    override func cancelOperation(_ sender: Any?) {
        close()
    }
}

/// The big blueprint card: question, or question and answer with its source,
/// or a message / the end-of-run summary.
final class FlashcardView: NSView {
    enum Content {
        case card(Flashcard, flipped: Bool)
        case message(String, String)
        case finished(FlashcardSession.Stats)
    }

    var content = Content.message("", "") {
        didSet { needsDisplay = true }
    }

    var onClick: (() -> Void)?
    private let palette: BlueprintPalette

    init(palette: BlueprintPalette) {
        self.palette = palette
        super.init(frame: .zero)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override var isFlipped: Bool { true }

    override func mouseDown(with event: NSEvent) {
        if case .card = content { onClick?() }
    }

    override func draw(_ dirtyRect: NSRect) {
        let sheet = bounds.insetBy(dx: 2, dy: 2)
        let shape = NSBezierPath(roundedRect: sheet, xRadius: 10, yRadius: 10)
        palette.pageBackground.setFill()
        shape.fill()
        NSGraphicsContext.saveGraphicsState()
        shape.addClip()
        palette.grid.setStroke()
        let grid = NSBezierPath()
        grid.lineWidth = 1
        for x in stride(from: sheet.minX + 20, to: sheet.maxX, by: 20) {
            grid.move(to: NSPoint(x: x, y: sheet.minY))
            grid.line(to: NSPoint(x: x, y: sheet.maxY))
        }
        for y in stride(from: sheet.minY + 20, to: sheet.maxY, by: 20) {
            grid.move(to: NSPoint(x: sheet.minX, y: y))
            grid.line(to: NSPoint(x: sheet.maxX, y: y))
        }
        grid.stroke()
        NSGraphicsContext.restoreGraphicsState()

        let text = sheet.insetBy(dx: 40, dy: 32)
        switch content {
        case let .card(card, flipped):
            guard flipped else {
                drawCentered([(card.question, font(26, .semibold), palette.text)], in: text)
                return
            }
            var parts: [(String, NSFont, NSColor)] = [
                (card.question, font(15, .medium), palette.muted),
                ("", font(10, .regular), palette.muted),
                (card.answer, font(22, .regular), palette.text),
            ]
            if let ref = card.ref, !ref.isEmpty {
                parts += [("", font(10, .regular), palette.muted), ("↳ " + ref, font(12, .regular), palette.accent)]
            }
            drawCentered(parts, in: text)
        case let .message(title, detail):
            drawCentered([(title, font(22, .semibold), palette.text), ("", font(8, .regular), palette.muted), (detail, font(14, .regular), palette.muted)], in: text)
        case .finished(let stats):
            let counts = CardRating.allCases.map { "\($0.title) \(stats.ratings[$0] ?? 0)" }.joined(separator: "   ·   ")
            drawCentered([
                ("Session complete", font(26, .semibold), palette.text),
                ("", font(8, .regular), palette.muted),
                ("\(stats.cards) card\(stats.cards == 1 ? "" : "s") studied, \(stats.answers) answer\(stats.answers == 1 ? "" : "s")", font(15, .regular), palette.text),
                (counts, font(13, .regular), palette.muted),
            ], in: text)
        }
    }

    private func font(_ size: CGFloat, _ weight: NSFont.Weight) -> NSFont {
        .systemFont(ofSize: size, weight: weight)
    }

    /// Lines of text, each centred, the block centred vertically.
    private func drawCentered(_ parts: [(String, NSFont, NSColor)], in rect: NSRect) {
        let paragraph = NSMutableParagraphStyle()
        paragraph.alignment = .center
        paragraph.lineSpacing = 3
        let text = NSMutableAttributedString()
        for (index, (string, font, color)) in parts.enumerated() {
            text.append(NSAttributedString(string: (index > 0 ? "\n" : "") + string, attributes: [
                .font: font, .foregroundColor: color, .paragraphStyle: paragraph,
            ]))
        }
        let size = text.boundingRect(with: NSSize(width: rect.width, height: .greatestFiniteMagnitude), options: [.usesLineFragmentOrigin, .usesFontLeading]).size
        let height = min(size.height, rect.height)
        text.draw(with: NSRect(x: rect.minX, y: rect.midY - height / 2, width: rect.width, height: height),
                  options: [.usesLineFragmentOrigin, .usesFontLeading, .truncatesLastVisibleLine])
    }
}
