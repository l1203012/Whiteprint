import AppKit
import WhiteprintCore

// Keyboard, mouse and menu behaviours. The decisions are made by the pure
// model (`MarkdownEditing`, `EditorDocument`, `SlashMenuState`); this file
// routes events to them and applies the results.

extension NoteEditorView: BlockTextViewDelegate {
    /// Applies a text change through the text view, so it's undoable as typing.
    func perform(_ change: TextChange, in view: BlockTextView, actionName: String? = nil) {
        view.breakUndoCoalescing()
        isPerformingChange = true
        if view.shouldChangeText(in: change.range, replacementString: change.replacement) {
            view.textStorage?.replaceCharacters(
                in: change.range,
                with: NSAttributedString(string: change.replacement, attributes: TextStyle.base(palette, fontSize: view.fontSize)))
            view.didChangeText()
        }
        isPerformingChange = false
        view.setSelectedRange(change.selection)
        view.breakUndoCoalescing()
        if let actionName { undoManager?.setActionName(actionName) }
    }

    func textView(_ textView: NSTextView, shouldChangeTextIn range: NSRange, replacementString string: String?) -> Bool {
        guard !isPerformingChange, let view = textView as? BlockTextView, let string else { return true }
        if let change = MarkdownEditing.shortcut(inserting: string, at: range, in: view.string) {
            perform(change, in: view)
            return false
        }
        if string == "/", MarkdownEditing.opensSlashMenu(at: range.location, in: view.string) {
            pendingSlash = (view.blockID, range.location)
        }
        return true
    }

    func textDidChange(_ view: NSTextView) {
        guard let view = view as? BlockTextView, textViews[view.blockID] === view else { return }
        document.setText(view.string, of: view.blockID)
        noteDidChange()
        if let slash = pendingSlash, slash.block == view.blockID {
            pendingSlash = nil
            openSlashMenu(in: view, at: slash.location)
        } else {
            updateSlashMenu(in: view)
        }
    }

    func textViewDidChangeSelection(_ view: NSTextView) {
        guard let view = view as? BlockTextView else { return }
        updateSlashMenu(in: view)
    }

    func textView(_ textView: NSTextView, doCommandBy selector: Selector) -> Bool {
        guard let view = textView as? BlockTextView else { return false }
        if slashMenu?.block == view.blockID, handleSlashMenuCommand(selector) {
            return true
        }
        let text = view.string
        let selection = view.selectedRange()
        let length = (text as NSString).length
        switch selector {
        case #selector(NSResponder.insertNewline(_:)):
            guard let change = MarkdownEditing.newline(in: text, selection: selection) else { return false }
            perform(change, in: view)
            return true
        case #selector(NSResponder.insertTab(_:)), #selector(NSResponder.insertBacktab(_:)):
            let outdent = selector == #selector(NSResponder.insertBacktab(_:))
            if let change = MarkdownEditing.indent(in: text, selection: selection, outdent: outdent) {
                perform(change, in: view, actionName: outdent ? "Outdent" : "Indent")
                return true
            }
            return outdent
        case #selector(NSResponder.moveUp(_:)):
            return selection.length == 0 && view.caretIsOnFirstLine
                && moveFocus(from: view.blockID, forward: false, x: view.caretX)
        case #selector(NSResponder.moveDown(_:)):
            return selection.length == 0 && view.caretIsOnLastLine
                && moveFocus(from: view.blockID, forward: true, x: view.caretX)
        case #selector(NSResponder.moveLeft(_:)):
            return selection == NSRange(location: 0, length: 0) && moveFocus(from: view.blockID, forward: false, x: nil)
        case #selector(NSResponder.moveRight(_:)):
            return selection == NSRange(location: length, length: 0) && moveFocus(from: view.blockID, forward: true, x: nil)
        case #selector(NSResponder.deleteBackward(_:)):
            return selection == NSRange(location: 0, length: 0) && backspaceAtStart(of: view.blockID)
        default:
            return false
        }
    }

    func blockTextView(_ view: BlockTextView, toggleCheckboxAt location: Int) {
        guard let change = MarkdownEditing.toggleCheckbox(at: location, in: view.string, selection: view.selectedRange()) else {
            return
        }
        perform(change, in: view, actionName: "Toggle Checkbox")
    }

    func blockTextViewDidBecomeFirstResponder(_ view: BlockTextView) {
        if let menu = slashMenu, menu.block != view.blockID {
            closeSlashMenu()
        }
    }

    func blockTextViewTouchBar(_ view: BlockTextView) -> NSTouchBar? {
        touchBarProvider.makeTouchBar()
    }

    // MARK: Moving between blocks

    /// Moves the focus to the previous or next block. A text block gets the
    /// caret at `x` on its last / first line (or at its end / start without
    /// `x`); a drawing or deck gets selected. Returns false at the ends of the note.
    @discardableResult
    func moveFocus(from id: BlockID, forward: Bool, x: CGFloat?) -> Bool {
        guard let target = forward ? document.block(after: id) : document.block(before: id) else { return false }
        guard let text = target.text else {
            focus(.block(target.id))
            return true
        }
        let length = (text as NSString).length
        focus(.text(target.id, NSRange(location: forward ? 0 : length, length: 0)))
        if let x, let view = textViews[target.id] {
            view.placeCaret(atX: x, onFirstLine: forward)
            view.scrollRangeToVisible(view.selectedRange())
        }
        return true
    }

    /// ⌫ at the start of a text block: selects a drawing or deck before it, joins it
    /// onto text before it, or removes the page break before it (deleting
    /// the page if it's empty).
    func backspaceAtStart(of id: BlockID) -> Bool {
        guard let location = document.location(of: id) else { return false }
        if location.block > 0 {
            let previous = document.pages[location.page].blocks[location.block - 1]
            if !previous.isText {
                focus(.block(previous.id))
                return true
            }
            performBlockEdit("Join Blocks") { document in
                document.mergeWithPrevious(id).map { .text($0.block, NSRange(location: $0.offset, length: 0)) }
            }
            return true
        }
        guard location.page > 0 else { return false }
        let page = document.pages[location.page]
        if page.blocks.count == 1, page.blocks[0].text?.isEmpty == true {
            performBlockEdit("Delete Page") { document in
                document.removePage(location.page)
                return document.pages[location.page - 1].blocks.last.map(endFocus)
            }
        } else {
            performBlockEdit("Join Pages") { document in
                document.mergePageWithPrevious(location.page)
                return .text(id, NSRange(location: 0, length: 0))
            }
        }
        return true
    }

    // MARK: Slash menu

    func openSlashMenu(in view: BlockTextView, at location: Int) {
        var state = SlashMenuState(slashLocation: location)
        guard state.update(text: view.string, caret: view.selectedRange().location) else { return }
        slashMenu = (view.blockID, state)
        slashMenuView.state = state
        slashMenuView.isHidden = false
        positionSlashMenu()
        if NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            slashMenuView.alphaValue = 1
        } else {
            slashMenuView.alphaValue = 0
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.12
                slashMenuView.animator().alphaValue = 1
            }
        }
    }

    func updateSlashMenu(in view: BlockTextView) {
        guard var menu = slashMenu, menu.block == view.blockID else { return }
        let selection = view.selectedRange()
        guard selection.length == 0, menu.state.update(text: view.string, caret: selection.location) else {
            closeSlashMenu()
            return
        }
        slashMenu = menu
        slashMenuView.state = menu.state
        positionSlashMenu()
    }

    func closeSlashMenu() {
        pendingSlash = nil
        guard slashMenu != nil else { return }
        slashMenu = nil
        slashMenuView.state = nil
        slashMenuView.isHidden = true
    }

    private func handleSlashMenuCommand(_ selector: Selector) -> Bool {
        guard var menu = slashMenu else { return false }
        switch selector {
        case #selector(NSResponder.moveUp(_:)):
            menu.state.moveSelection(by: -1)
        case #selector(NSResponder.moveDown(_:)):
            menu.state.moveSelection(by: 1)
        case #selector(NSResponder.insertNewline(_:)), #selector(NSResponder.insertTab(_:)):
            chooseSlashCommand(at: nil)
            return true
        case #selector(NSResponder.cancelOperation(_:)):
            closeSlashMenu()
            return true
        default:
            return false
        }
        slashMenu = menu
        slashMenuView.state = menu.state
        return true
    }

    /// Runs the selected (or the given row's) command.
    func chooseSlashCommand(at row: Int?) {
        guard var menu = slashMenu, let view = textViews[menu.block] else { return }
        if let row { menu.state.select(row) }
        closeSlashMenu()
        guard let command = menu.state.selected else { return }
        window?.makeFirstResponder(view)
        switch MarkdownEditing.apply(command, in: view.string, slash: menu.state.typedRange) {
        case .text(let change):
            perform(change, in: view, actionName: command.title)
        case .insertDrawing(let removal):
            perform(removal, in: view)
            insertDrawing(source: "", splitting: view.blockID, at: removal.selection.location, page: currentPageIndex)
        case .insertDeck(let removal):
            perform(removal, in: view)
            insertDeck(splitting: view.blockID, at: removal.selection.location, page: currentPageIndex)
        case .newPage(let removal):
            perform(removal, in: view)
            addPage(after: document.location(of: view.blockID)?.page ?? currentPageIndex)
        }
    }

    func positionSlashMenu() {
        guard let menu = slashMenu, let view = textViews[menu.block], view.window != nil else {
            slashMenuView.isHidden = true
            return
        }
        let anchor = convert(view.rect(for: NSRange(location: menu.state.slashLocation, length: 1)), from: view)
        let size = slashMenuView.preferredSize
        let margin = SlashMenuView.shadowMargin
        var origin = NSPoint(x: anchor.minX - 6 - margin, y: anchor.maxY + 6 - margin)
        if origin.y + size.height - margin > bounds.maxY - 8, anchor.minY - 6 - size.height + margin > bounds.minY + 8 {
            origin.y = anchor.minY - 6 - size.height + margin
        }
        origin.x = max(bounds.minX + 8 - margin, min(origin.x, bounds.maxX - size.width + margin - 8))
        slashMenuView.frame = NSRect(origin: origin, size: size)
        slashMenuView.isHidden = false
    }

    // MARK: Block operations

    func insertDrawing(source: String, splitting text: BlockID?, at offset: Int?, page: Int) {
        let focus = performBlockEdit("Insert Drawing") { document in
            if let text, let offset, let id = document.insertDrawing(source: source, splitting: text, at: offset) {
                return .block(id)
            }
            return .block(document.appendDrawing(source: source, toPage: page))
        }
        if case .block(let id) = focus {
            layoutSubtreeIfNeeded()
            editDrawing(id)
        }
    }

    func insertDeck(splitting text: BlockID?, at offset: Int?, page: Int) {
        let empty = CardDeck(cards: [])
        let focus = performBlockEdit("Insert Flashcards") { document in
            if let text, let offset, let id = document.insertDeck(empty, splitting: text, at: offset) {
                return .block(id)
            }
            return .block(document.appendDeck(empty, toPage: page))
        }
        if case .block(let id) = focus {
            layoutSubtreeIfNeeded()
            editDeck(id)
        }
    }

    func addPage(after page: Int) {
        let focus = performBlockEdit("Add Page") { document in
            .text(document.insertPage(after: page), NSRange(location: 0, length: 0))
        }
        if focus != nil {
            scrollToPage(page + 2)
        }
    }

    func deleteBlock(_ id: BlockID) {
        let name = document.block(id)?.drawing != nil ? "Delete Drawing" : document.block(id)?.deck != nil ? "Delete Flashcards" : "Delete Block"
        performBlockEdit(name) { document in
            let before = document.block(before: id)
            let after = document.block(after: id)
            let fallback = document.removeBlock(id)
            if let before, before.isText, document.block(before.id) != nil { return endFocus(of: before) }
            if let after, after.isText, document.block(after.id) != nil {
                return .text(after.id, NSRange(location: 0, length: 0))
            }
            return fallback.flatMap { document.block($0) }.map(endFocus)
        }
    }

    func duplicateBlock(_ id: BlockID) {
        performBlockEdit("Duplicate Block") { document in
            document.duplicateBlock(id)
            return nil
        }
    }

    func moveBlock(_ id: BlockID, by delta: Int) {
        let wasFocused = currentFocus
        performBlockEdit(delta < 0 ? "Move Block Up" : "Move Block Down") { document in
            document.moveBlock(id, by: delta) ? wasFocused : nil
        }
    }

    // MARK: Handle

    func updateHandle(at point: NSPoint) {
        guard let (page, view) = blockView(at: point) else {
            if !handle.frame.insetBy(dx: -6, dy: -6).contains(point) { handle.hide() }
            return
        }
        let id: BlockID
        let lineMid: CGFloat
        switch view {
        case let text as BlockTextView:
            guard !text.string.isEmpty else {
                handle.hide()
                return
            }
            id = text.blockID
            lineMid = text.lineRect(at: 0).midY
        case let drawing as DrawingBlockView:
            id = drawing.blockID
            lineMid = DrawingBlockView.verticalPadding + 12
        case let deck as DeckBlockView:
            id = deck.blockID
            let metrics = DeckLayout.metrics(deck.fontSize)
            lineMid = metrics.padding + metrics.headerHeight / 2
        default:
            return
        }
        let frame = documentView.convert(view.frame, from: page)
        let size = BlockHandleView.size
        handle.show(for: id, at: NSPoint(x: frame.minX - size.width - 10, y: (frame.minY + lineMid - size.height / 2).rounded()))
    }

    /// The block whose row (the full sheet width) is under `point`.
    private func blockView(at point: NSPoint) -> (PageView, NSView)? {
        guard let page = pageViews.first(where: { $0.frame.contains(point) }), page.isRealized else { return nil }
        let local = page.convert(point, from: documentView)
        guard page.sheetRect.contains(local) else { return nil }
        let half = geometry.blockSpacing / 2
        guard let view = page.blockViews.first(where: { local.y >= $0.frame.minY - half && local.y < $0.frame.maxY + half }) else {
            return nil
        }
        return (page, view)
    }

    func showBlockMenu(from handle: BlockHandleView) {
        guard let id = handle.blockID else { return }
        let menu = NSMenu()
        menu.autoenablesItems = false
        func add(_ title: String, _ action: Selector, enabled: Bool = true) {
            let item = NSMenuItem(title: title, action: action, keyEquivalent: "")
            item.target = self
            item.representedObject = id.rawValue
            item.isEnabled = enabled
            menu.addItem(item)
        }
        var probe = document
        let canMoveUp = probe.moveBlock(id, by: -1)
        probe = document
        let canMoveDown = probe.moveBlock(id, by: 1)
        add("Delete", #selector(blockMenuDelete(_:)))
        add("Duplicate", #selector(blockMenuDuplicate(_:)))
        menu.addItem(.separator())
        add("Move Up", #selector(blockMenuMoveUp(_:)), enabled: canMoveUp)
        add("Move Down", #selector(blockMenuMoveDown(_:)), enabled: canMoveDown)
        if document.block(id)?.drawing != nil {
            menu.addItem(.separator())
            add("Edit Drawing…", #selector(blockMenuEditDrawing(_:)))
        }
        if let deck = document.block(id)?.deck {
            menu.addItem(.separator())
            add("Edit Flashcards…", #selector(blockMenuEditDeck(_:)))
            add("Study", #selector(blockMenuStudy(_:)), enabled: !deck.cards.isEmpty)
        }
        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: handle.bounds.maxY + 4), in: handle)
    }

    private func menuBlock(_ item: NSMenuItem) -> BlockID? {
        (item.representedObject as? Int).map(BlockID.init)
    }

    @objc private func blockMenuDelete(_ item: NSMenuItem) {
        if let id = menuBlock(item) { deleteBlock(id) }
    }

    @objc private func blockMenuDuplicate(_ item: NSMenuItem) {
        if let id = menuBlock(item) { duplicateBlock(id) }
    }

    @objc private func blockMenuMoveUp(_ item: NSMenuItem) {
        if let id = menuBlock(item) { moveBlock(id, by: -1) }
    }

    @objc private func blockMenuMoveDown(_ item: NSMenuItem) {
        if let id = menuBlock(item) { moveBlock(id, by: 1) }
    }

    @objc private func blockMenuEditDrawing(_ item: NSMenuItem) {
        if let id = menuBlock(item) { editDrawing(id) }
    }

    @objc private func blockMenuEditDeck(_ item: NSMenuItem) {
        if let id = menuBlock(item) { editDeck(id) }
    }

    @objc private func blockMenuStudy(_ item: NSMenuItem) {
        if let id = menuBlock(item), let deck = document.block(id)?.deck { onStudyDeck?(deck) }
    }

    // MARK: Selected drawings and decks

    /// Keys on a selected drawing or deck: ⌫ deletes it, ↩ starts text after
    /// it, space opens its editor, arrows move to the neighbouring blocks.
    func handleKey(_ event: NSEvent, onSelected id: BlockID) -> Bool {
        switch event.keyCode {
        case 51, 117:
            deleteBlock(id)
        case 36, 76:
            performBlockEdit("New Line") { document in
                document.textBlock(after: id).map { .text($0, NSRange(location: 0, length: 0)) }
            }
            if case .block = currentFocus, let next = document.block(after: id), next.isText {
                focus(.text(next.id, NSRange(location: 0, length: 0)))
            }
        case 49:
            if document.block(id)?.deck != nil { editDeck(id) } else { editDrawing(id) }
        case 123, 126:
            moveFocus(from: id, forward: false, x: nil)
        case 124, 125:
            moveFocus(from: id, forward: true, x: nil)
        default:
            return false
        }
        return true
    }
}

// MARK: - Drawings

extension NoteEditorView: DrawingBlockViewDelegate {
    func drawingBlockViewRequestsEditor(_ view: DrawingBlockView) {
        editDrawing(view.blockID)
    }

    func drawingBlockView(_ view: DrawingBlockView, handleKey event: NSEvent) -> Bool {
        handleKey(event, onSelected: view.blockID)
    }

    /// Opens the source popover for a drawing (or, offscreen, just the editor).
    func editDrawing(_ id: BlockID) {
        guard let view = view(for: id) as? DrawingBlockView else { return }
        closeDrawingEditor(commit: true)
        closeDeckEditor(commit: true)
        let editor = DrawingSourceEditor(blockID: id, source: view.source, palette: palette) { [weak self] source in
            self?.commitDrawing(id, source: source)
        }
        drawingEditor = editor
        guard let window, window.isVisible else { return }
        let popover = NSPopover()
        popover.behavior = .transient
        popover.animates = !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        popover.contentViewController = editor
        popover.delegate = editor
        editor.popover = popover
        popover.show(relativeTo: view.bounds, of: view, preferredEdge: .maxY)
    }

    func closeDrawingEditor(commit: Bool) {
        guard let editor = drawingEditor else { return }
        drawingEditor = nil
        if commit {
            editor.close()
        } else {
            editor.discard()
        }
    }

    func commitDrawing(_ id: BlockID, source: String) {
        if drawingEditor?.blockID == id { drawingEditor = nil }
        guard let drawing = document.block(id)?.drawing, drawing.source != source else { return }
        performBlockEdit("Edit Drawing") { document in
            document.updateDrawing(id, source: source)
            return nil
        }
    }
}

// MARK: - Flashcard decks

extension NoteEditorView: DeckBlockViewDelegate {
    func deckBlockViewRequestsEditor(_ view: DeckBlockView) {
        editDeck(view.blockID)
    }

    func deckBlockViewRequestsStudy(_ view: DeckBlockView) {
        if let deck = document.block(view.blockID)?.deck { onStudyDeck?(deck) }
    }

    func deckBlockView(_ view: DeckBlockView, handleKey event: NSEvent) -> Bool {
        handleKey(event, onSelected: view.blockID)
    }

    func deckBlockViewDidChangeHeight(_ view: DeckBlockView) {
        needsLayout = true
    }

    /// Opens the editor popover for a deck (or, offscreen, just the editor).
    func editDeck(_ id: BlockID) {
        guard let view = view(for: id) as? DeckBlockView, let deck = document.block(id)?.deck else { return }
        closeDrawingEditor(commit: true)
        closeDeckEditor(commit: true)
        let editor = DeckEditor(blockID: id, deck: deck) { [weak self] deck in
            self?.commitDeck(id, deck)
        }
        deckEditor = editor
        guard let window, window.isVisible else { return }
        let popover = NSPopover()
        popover.behavior = .transient
        popover.animates = !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        popover.contentViewController = editor
        popover.delegate = editor
        editor.popover = popover
        popover.show(relativeTo: view.bounds, of: view, preferredEdge: .maxY)
    }

    func closeDeckEditor(commit: Bool) {
        guard let editor = deckEditor else { return }
        deckEditor = nil
        if commit {
            editor.close()
        } else {
            editor.discard()
        }
    }

    /// Saves a deck's new title and cards as one undoable step.
    func commitDeck(_ id: BlockID, _ deck: CardDeck) {
        if deckEditor?.blockID == id { deckEditor = nil }
        guard let old = document.block(id)?.deck, old.title != deck.title || old.cards != deck.cards else { return }
        performBlockEdit("Edit Flashcards") { document in
            document.updateDeck(id, deck)
            return nil
        }
    }
}

/// Receives the text views' delegate calls and hands them to the editor, so
/// `NoteEditorView` doesn't publish an `NSTextViewDelegate` conformance.
final class TextViewRouter: NSObject, NSTextViewDelegate {
    weak var editor: NoteEditorView?

    func textView(_ textView: NSTextView, shouldChangeTextIn range: NSRange, replacementString string: String?) -> Bool {
        editor?.textView(textView, shouldChangeTextIn: range, replacementString: string) ?? true
    }

    func textDidChange(_ notification: Notification) {
        if let view = notification.object as? NSTextView { editor?.textDidChange(view) }
    }

    func textViewDidChangeSelection(_ notification: Notification) {
        if let view = notification.object as? NSTextView { editor?.textViewDidChangeSelection(view) }
    }

    func textView(_ textView: NSTextView, doCommandBy selector: Selector) -> Bool {
        editor?.textView(textView, doCommandBy: selector) ?? false
    }
}
