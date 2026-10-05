import AppKit
import WhiteprintCore
import WhiteprintRender

/// The Notion-style editing surface for one note: a scrolling, centred column
/// of blue blueprint pages with white text, inline drawings, slash menu,
/// Markdown shortcuts and checkboxes.
///
/// The content lives in an `EditorDocument`; views are thin and keyed by
/// block id. Pages near the viewport get views; the rest are estimated.
public final class NoteEditorView: NSView {
    /// The current content, including unsaved edits.
    public private(set) var note: Note

    /// Called after each user edit (coalesced, at most every ~300 ms).
    public var onChange: ((Note) -> Void)?

    /// Called when the page nearest the top of the viewport changes (1-based).
    public var onVisiblePageChange: ((Int) -> Void)?

    let palette: BlueprintPalette
    var document: EditorDocument
    let scrollView = NSScrollView()
    let documentView = EditorDocumentView()
    let handle: BlockHandleView
    let slashMenuView = SlashMenuView()
    var slashMenu: (block: BlockID, state: SlashMenuState)?
    /// A `/` just typed at this place opens the menu once the edit lands.
    var pendingSlash: (block: BlockID, location: Int)?
    var drawingEditor: DrawingSourceEditor?
    var isPerformingChange = false

    private(set) var pageViews: [PageView] = []
    private var pageViewsByID: [PageID: PageView] = [:]
    /// Block views by id. Views of removed blocks are kept so an undo that
    /// brings a block back also brings back its view and typing undo.
    private(set) var textViews: [BlockID: BlockTextView] = [:]
    private(set) var drawingViews: [BlockID: DrawingBlockView] = [:]
    private let changes = ChangeCoalescer(delay: 0.3)
    private let textRouter = TextViewRouter()
    private(set) var visiblePage = 1
    private var isSyncing = false
    private var isLayingOut = false

    public init(note: Note, palette: BlueprintPalette = .blueprint) {
        self.palette = palette
        document = EditorDocument(note: note)
        self.note = document.note
        handle = BlockHandleView(palette: palette)
        super.init(frame: .zero)
        textRouter.editor = self
        setUpViews()
        syncViews()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    public override var isFlipped: Bool { true }

    /// Replaces the content, e.g. after Claude edited the note over MCP. With
    /// `preservingSelection`, the caret and scroll position stay where they were
    /// as far as possible.
    ///
    /// The change is registered as one undoable step, which also keeps the
    /// text views' own typing undo consistent with their content.
    public func setNote(_ note: Note, preservingSelection: Bool) {
        guard note != self.note else { return }
        closeSlashMenu()
        let focus = currentFocus
        let anchor = scrollAnchor()
        let before = Snapshot(document: document, focus: focus)
        let old = document
        document = document.reconciled(with: note)
        self.note = document.note
        syncViews()
        if let editor = drawingEditor, document.block(editor.blockID)?.drawing == nil {
            closeDrawingEditor(commit: false)
        }
        if preservingSelection {
            restoreScroll(anchor)
            if let focus { self.focus(transferred(focus, from: old), scroll: false) }
        } else {
            if focus != nil { window?.makeFirstResponder(nil) }
            scroll(toY: 0)
        }
        registerUndo(restoring: before, name: "External Edit")
        updateVisiblePage()
    }

    /// 1-based.
    public func scrollToPage(_ page: Int) {
        guard !pageViews.isEmpty else { return }
        let index = min(max(page - 1, 0), pageViews.count - 1)
        layoutSubtreeIfNeeded()
        realize(page: index)
        let sheetTop = pageViews[index].frame.minY + PageGeometry.shadowInset
        scroll(toY: sheetTop - PageGeometry.pageGap)
        realizeVisiblePages()
        // Realizing pages above can shift this one; settle on it once more.
        scroll(toY: pageViews[index].frame.minY + PageGeometry.shadowInset - PageGeometry.pageGap)
        updateVisiblePage()
    }

    /// Inserts a drawing at the caret (or the end of the current page) and
    /// opens its source popover.
    public func insertDrawingAtSelection(source: String = "") {
        if case .text(let id, let range) = currentFocus {
            insertDrawing(source: source, splitting: id, at: range.location, page: currentPageIndex)
        } else {
            insertDrawing(source: source, splitting: nil, at: nil, page: currentPageIndex)
        }
    }

    /// Adds a page after the current one and moves the caret there.
    public func addPage() {
        addPage(after: currentPageIndex)
    }

    // MARK: Setup and layout

    private func setUpViews() {
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.horizontalScrollElasticity = .none
        scrollView.autohidesScrollers = true
        scrollView.borderType = .noBorder
        scrollView.drawsBackground = true
        scrollView.backgroundColor = .windowBackgroundColor
        scrollView.documentView = documentView
        scrollView.contentView.postsBoundsChangedNotifications = true
        NotificationCenter.default.addObserver(self, selector: #selector(didScroll),
                                               name: NSView.boundsDidChangeNotification, object: scrollView.contentView)
        addSubview(scrollView)

        slashMenuView.isHidden = true
        slashMenuView.onChoose = { [weak self] row in self?.chooseSlashCommand(at: row) }
        addSubview(slashMenuView)

        documentView.addSubview(handle)
        documentView.onMouseMoved = { [weak self] point in self?.updateHandle(at: point) }
        documentView.onMouseExited = { [weak self] in self?.handle.hide() }
        handle.onClick = { [weak self] handle in self?.showBlockMenu(from: handle) }
    }

    public override func layout() {
        super.layout()
        if scrollView.frame != bounds {
            scrollView.frame = bounds
        }
        layoutDocument()
        realizeVisiblePages()
        positionSlashMenu()
    }

    var geometry: PageGeometry {
        PageGeometry(documentWidth: scrollView.contentSize.width)
    }

    /// Stacks the pages, keeping the page at the top of the viewport in place
    /// when pages above it change height.
    func layoutDocument() {
        guard !isLayingOut else { return }
        isLayingOut = true
        defer { isLayingOut = false }
        let width = scrollView.contentSize.width
        guard width > 0 else { return }
        let geometry = PageGeometry(documentWidth: width)
        let anchor = scrollAnchor()
        let inset = PageGeometry.shadowInset
        var sheetTop = PageGeometry.pageGap
        for (index, page) in pageViews.enumerated() {
            if !page.isRealized, page.estimatedWidth != geometry.textWidth {
                page.estimatedContentHeight = estimatedHeight(of: document.pages[index], width: geometry.textWidth)
                page.estimatedWidth = geometry.textWidth
            }
            let height = page.layoutBlocks(geometry)
            let frame = NSRect(x: geometry.pageX - inset, y: sheetTop - inset,
                               width: geometry.pageWidth + 2 * inset, height: height + 2 * inset)
            if page.frame != frame { page.frame = frame }
            sheetTop += height + PageGeometry.pageGap
        }
        let height = max(sheetTop + PageGeometry.pageGap, scrollView.contentSize.height)
        if documentView.frame.size != NSSize(width: width, height: height) {
            documentView.setFrameSize(NSSize(width: width, height: height))
        }
        restoreScroll(anchor)
        handle.hide()
    }

    private func estimatedHeight(of page: EditorPage, width: CGFloat) -> CGFloat {
        let blocks = page.blocks.map { block -> CGFloat in
            switch block.content {
            case .text(let text): return HeightEstimate.text(text, width: width)
            case .drawing(let drawing):
                return DrawingBlockView.height(for: DrawingBlockView.canvasSize(for: drawing.source), width: width)
            case .cards(let deck):
                return HeightEstimate.text(DeckPlaceholderView.text(for: deck), width: width)
            }
        }
        return blocks.reduce(0, +) + CGFloat(max(0, blocks.count - 1)) * PageGeometry.blockSpacing
    }

    struct ScrollAnchor {
        var page: Int
        var offset: CGFloat
    }

    /// The page at the top of the viewport and how far into it we've scrolled.
    func scrollAnchor() -> ScrollAnchor? {
        let top = scrollView.contentView.bounds.minY
        guard top > 0, let index = pageViews.firstIndex(where: { $0.frame.maxY > top }) else { return nil }
        return ScrollAnchor(page: index, offset: top - pageViews[index].frame.minY)
    }

    func restoreScroll(_ anchor: ScrollAnchor?) {
        guard let anchor, pageViews.indices.contains(anchor.page) else { return }
        let y = pageViews[anchor.page].frame.minY + anchor.offset
        if abs(y - scrollView.contentView.bounds.minY) > 0.5 {
            scroll(toY: y)
        }
    }

    func scroll(toY y: CGFloat) {
        let maxY = max(0, documentView.frame.height - scrollView.contentView.bounds.height)
        scrollView.contentView.scroll(to: NSPoint(x: 0, y: min(max(0, y), maxY)))
        scrollView.reflectScrolledClipView(scrollView.contentView)
    }

    @objc private func didScroll() {
        handle.hide()
        realizeVisiblePages()
        positionSlashMenu()
        updateVisiblePage()
    }

    private func updateVisiblePage() {
        let top = scrollView.contentView.bounds.minY + PageGeometry.pageGap
        let index = pageViews.firstIndex { $0.frame.maxY - PageGeometry.shadowInset > top } ?? max(0, pageViews.count - 1)
        if index + 1 != visiblePage {
            visiblePage = index + 1
            onVisiblePageChange?(visiblePage)
        }
    }

    // MARK: Model → views

    /// Brings the page and block views in line with `document`.
    func syncViews() {
        var views: [PageView] = []
        for page in document.pages {
            let view = pageViewsByID[page.id] ?? makePageView(page.id)
            if view.isRealized {
                view.setBlockViews(page.blocks.map { blockView(for: $0, alone: page.blocks.count == 1) })
            } else {
                view.estimatedWidth = 0
            }
            views.append(view)
        }
        for old in pageViews where !views.contains(old) {
            old.removeFromSuperview()
            pageViewsByID[old.pageID] = nil
        }
        for view in views where view.superview == nil {
            documentView.addSubview(view, positioned: .below, relativeTo: handle)
        }
        pageViews = views
        layoutDocument()
        realizeVisiblePages()
    }

    private func makePageView(_ id: PageID) -> PageView {
        let view = PageView(pageID: id, palette: palette)
        view.onClickBelowBlocks = { [weak self] page in self?.focusEnd(of: page) }
        pageViewsByID[id] = view
        return view
    }

    private func blockView(for block: EditorBlock, alone: Bool) -> NSView {
        switch block.content {
        case .text(let text):
            let view = textViews[block.id] ?? makeTextView(block.id, text: text)
            isSyncing = true
            view.setText(text)
            isSyncing = false
            view.isAlonePlaceholder = alone
            return view
        case .drawing(let drawing):
            let view = drawingViews[block.id] ?? makeDrawingView(block.id, source: drawing.source)
            view.source = drawing.source
            view.needsLayout = true
            view.needsDisplay = true
            return view
        case .cards(let deck):
            // TEMPORARY until the deck block view lands: a read-only summary.
            return DeckPlaceholderView(deck: deck, palette: palette)
        }
    }

    private func makeTextView(_ id: BlockID, text: String) -> BlockTextView {
        let view = BlockTextView(blockID: id, text: text, palette: palette, width: max(geometry.textWidth, 100))
        view.delegate = textRouter
        view.blockDelegate = self
        textViews[id] = view
        return view
    }

    private func makeDrawingView(_ id: BlockID, source: String) -> DrawingBlockView {
        let view = DrawingBlockView(blockID: id, source: source, palette: palette)
        view.delegate = self
        drawingViews[id] = view
        return view
    }

    /// Creates the block views of a page that hasn't been shown yet.
    func realize(page index: Int) {
        guard pageViews.indices.contains(index), !pageViews[index].isRealized else { return }
        let blocks = document.pages[index].blocks
        pageViews[index].isRealized = true
        pageViews[index].setBlockViews(blocks.map { blockView(for: $0, alone: blocks.count == 1) })
        layoutDocument()
    }

    /// The part of the document worth having views for: the viewport plus a
    /// screen's height above and below.
    private var nearbyRect: NSRect {
        let visible = scrollView.contentView.bounds
        return visible.insetBy(dx: 0, dy: -max(visible.height, 400))
    }

    /// Realizes the pages near the viewport and attaches them.
    func realizeVisiblePages() {
        guard scrollView.contentSize.width > 0 else { return }
        for _ in 0..<4 {
            let range = nearbyRect
            let pending = pageViews.indices.filter { !pageViews[$0].isRealized && pageViews[$0].frame.intersects(range) }
            guard !pending.isEmpty else { return }
            for index in pending { realize(page: index) }
        }
    }

    /// The view for a block, realizing its page if needed.
    func view(for id: BlockID) -> NSView? {
        guard let location = document.location(of: id) else { return nil }
        if !pageViews[location.page].isRealized {
            realize(page: location.page)
        }
        return textViews[id] ?? drawingViews[id]
    }

    // MARK: Changes

    func noteDidChange() {
        note = document.note
        changes.schedule { [weak self] in
            guard let self else { return }
            self.onChange?(self.note)
        }
    }

    /// Delivers a pending `onChange` immediately.
    func flushPendingChange() {
        changes.flush()
    }

    func blockTextViewDidChangeHeight(_ view: BlockTextView) {
        if !isLayingOut { needsLayout = true }
    }

    // MARK: Focus

    enum Focus: Equatable {
        case text(BlockID, NSRange)
        case drawing(BlockID)
    }

    var currentFocus: Focus? {
        switch window?.firstResponder {
        case let text as BlockTextView where textViews[text.blockID] === text && document.block(text.blockID) != nil:
            return .text(text.blockID, text.selectedRange())
        case let drawing as DrawingBlockView where document.block(drawing.blockID) != nil:
            return .drawing(drawing.blockID)
        default:
            return nil
        }
    }

    func focus(_ focus: Focus?, scroll: Bool = true) {
        guard let focus else { return }
        switch focus {
        case let .text(id, range):
            guard let view = view(for: id) as? BlockTextView else { return }
            layoutSubtreeIfNeeded()
            window?.makeFirstResponder(view)
            let length = (view.string as NSString).length
            let start = min(range.location, length)
            view.setSelectedRange(NSRange(location: start, length: min(range.length, length - start)))
            if scroll { view.scrollRangeToVisible(view.selectedRange()) }
        case .drawing(let id):
            guard let view = view(for: id) as? DrawingBlockView else { return }
            layoutSubtreeIfNeeded()
            window?.makeFirstResponder(view)
            if scroll { view.scrollToVisible(view.bounds) }
        }
    }

    /// The focus after an external change: same block if it survived (caret
    /// moved along with the text around it), otherwise the block now at its place.
    private func transferred(_ focus: Focus, from old: EditorDocument) -> Focus? {
        switch focus {
        case let .text(id, range):
            if let new = document.block(id)?.text, let previous = old.block(id)?.text {
                return .text(id, CaretTransform.transform(range, from: previous, to: new))
            }
            return old.location(of: id).flatMap { focusTarget(near: $0, caret: range.location) }
        case .drawing(let id):
            if document.block(id) != nil { return .drawing(id) }
            return old.location(of: id).flatMap { focusTarget(near: $0, caret: 0) }
        }
    }

    private func focusTarget(near location: BlockLocation, caret: Int) -> Focus? {
        guard !document.pages.isEmpty else { return nil }
        let page = min(location.page, document.pages.count - 1)
        let blocks = document.pages[page].blocks
        let block = blocks[min(location.block, blocks.count - 1)]
        if let text = block.text {
            return .text(block.id, NSRange(location: min(caret, (text as NSString).length), length: 0))
        }
        return .drawing(block.id)
    }

    /// The 0-based page of the focused block, or the visible page.
    var currentPageIndex: Int {
        switch currentFocus {
        case .text(let id, _), .drawing(let id):
            if let location = document.location(of: id) { return location.page }
        case nil:
            break
        }
        return min(visiblePage - 1, document.pages.count - 1)
    }

    private func focusEnd(of page: PageView) {
        guard let index = pageViews.firstIndex(of: page), let last = document.pages[index].blocks.last else { return }
        focus(endFocus(of: last))
    }

    func endFocus(of block: EditorBlock) -> Focus {
        if let text = block.text {
            return .text(block.id, NSRange(location: (text as NSString).length, length: 0))
        }
        return .drawing(block.id)
    }

    // MARK: Undo

    struct Snapshot {
        var document: EditorDocument
        var focus: Focus?
    }

    /// Applies a block-level edit as one undoable step. `body` returns where
    /// the focus goes afterwards (nil keeps it where it is).
    @discardableResult
    func performBlockEdit(_ name: String, _ body: (inout EditorDocument) -> Focus?) -> Focus? {
        closeSlashMenu()
        let before = Snapshot(document: document, focus: currentFocus)
        var edited = document
        let after = body(&edited)
        guard edited != document else { return nil }
        document = edited
        syncViews()
        noteDidChange()
        focus(after)
        registerUndo(restoring: before, name: name)
        return after
    }

    func registerUndo(restoring snapshot: Snapshot, name: String) {
        guard let undoManager else { return }
        for view in textViews.values { view.breakUndoCoalescing() }
        undoManager.registerUndo(withTarget: self) { editor in
            let current = Snapshot(document: editor.document, focus: editor.currentFocus)
            editor.restore(snapshot)
            editor.registerUndo(restoring: current, name: name)
        }
        undoManager.setActionName(name)
    }

    private func restore(_ snapshot: Snapshot) {
        closeSlashMenu()
        if let editor = drawingEditor, snapshot.document.block(editor.blockID)?.drawing == nil {
            closeDrawingEditor(commit: false)
        }
        document = document.restoring(snapshot.document)
        syncViews()
        noteDidChange()
        focus(snapshot.focus)
    }
}
