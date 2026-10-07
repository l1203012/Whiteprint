import AppKit
import WhiteprintCore
import WhiteprintEditor

/// One note's window: the editor on the page colour, the sidebar as a
/// floating glass panel, and glass pills in the titlebar (sidebar toggle,
/// the open notes, Markdown lens and More). Note windows open as tabs of the
/// frontmost note window; the tab strip pill stands in for the system tab bar.
final class NoteWindowController: NSWindowController, NSWindowDelegate, NSToolbarDelegate, NSMenuItemValidation {
    static let tabbingIdentifier = "io.github.l1203012.whiteprint.note"

    private(set) weak var noteDocument: NoteDocument?
    private let editor: NoteEditorView
    private let sidebar: SidebarViewController
    private let split = ChromeSplitViewController()
    private let editorController: EditorViewController
    private let sidebarPanel: GlassPanelController
    private lazy var sidebarButton = ChromeButton(symbol: "sidebar.left", size: NSSize(width: 30, height: 30), label: "Toggle Sidebar",
                                                  target: nil, action: #selector(NSSplitViewController.toggleSidebar(_:)))
    private let tabStrip = NoteTabStrip()
    private lazy var lensButton = ChromeButton(title: "MD", size: NSSize(width: 38, height: 30), label: "Show Markdown",
                                               target: self, action: #selector(toggleMarkdownLens(_:)))
    private lazy var moreButton = ChromeButton(symbol: "ellipsis", size: NSSize(width: 30, height: 30), label: "More",
                                               target: self, action: #selector(showMoreMenu(_:)))
    /// Pushes the tab strip past the open sidebar panel.
    private let titleSpacer = NSView()
    private lazy var titleSpacerWidth = titleSpacer.widthAnchor.constraint(equalToConstant: 0)
    private var observers: [NSObjectProtocol] = []
    private let touchBarProvider = NoteTouchBar()
    private(set) var visiblePage = 1
    private var pageTheme = ViewPreferences.shared.pageTheme

    init(document: NoteDocument) {
        noteDocument = document
        editor = NoteEditorView(note: document.note, palette: pageTheme.palette)
        sidebar = SidebarViewController()
        editorController = EditorViewController(editor: editor)
        sidebarPanel = GlassPanelController(content: sidebar)

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1080, height: 760),
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered, defer: true
        )
        window.minSize = NSSize(width: 560, height: 420)
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.titlebarSeparatorStyle = .none
        window.backgroundColor = pageTheme.palette.canvas
        window.toolbarStyle = .unified
        window.tabbingMode = .preferred
        window.tabbingIdentifier = Self.tabbingIdentifier

        let sidebarItem = NSSplitViewItem(viewController: sidebarPanel)
        sidebarItem.minimumThickness = 232
        sidebarItem.maximumThickness = 360
        sidebarItem.canCollapse = true
        split.addSplitViewItem(sidebarItem)
        let contentItem = NSSplitViewItem(viewController: editorController)
        contentItem.minimumThickness = 360
        split.addSplitViewItem(contentItem)
        split.splitView.autosaveName = "NoteWindowSplit"
        window.contentViewController = split
        window.setContentSize(NSSize(width: 1080, height: 760))

        super.init(window: window)
        window.delegate = self
        shouldCascadeWindows = true
        windowFrameAutosaveName = "NoteWindow"

        let toolbar = NSToolbar(identifier: "NoteToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false
        window.toolbar = toolbar

        sidebarButton.setAccessibilityLabel("Toggle Sidebar")
        lensButton.fillsWhenOn = true
        tabStrip.onClickSelected = { [weak self] tab in self?.showTitleMenu(from: tab) }
        tabStrip.onSelect = { window in window.tabGroup?.selectedWindow = window }
        titleSpacerWidth.isActive = true

        sidebar.windowController = self
        editor.onChange = { [weak document] note in document?.editorDidChange(note) }
        editor.onVisiblePageChange = { [weak self] page in self?.visiblePageChanged(page) }
        editor.onStudyDeck = { [weak self] deck in self?.study(deck) }
        let center = NotificationCenter.default
        observers = [
            center.addObserver(forName: .noteDocumentDidChange, object: document, queue: .main) { [weak self] notification in
                self?.documentChanged(origin: notification.userInfo?["origin"] as? NoteDocument.ChangeOrigin)
            },
            center.addObserver(forName: .noteDocumentDidMove, object: document, queue: .main) { [weak self] _ in
                self?.updateTitle()
            },
            center.addObserver(forName: .viewPreferencesDidChange, object: nil, queue: .main) { [weak self] _ in
                self?.applyViewPreferences()
            },
            center.addObserver(forName: .noteTabsDidChange, object: nil, queue: .main) { [weak self] _ in
                self?.updateTabs()
            },
            center.addObserver(forName: NSWindow.willCloseNotification, object: nil, queue: .main) { [weak self] _ in
                // The closing window is still in its tab group until this returns.
                DispatchQueue.main.async { self?.updateTabs() }
            },
            center.addObserver(forName: NSSplitView.didResizeSubviewsNotification, object: split.splitView, queue: .main) { [weak self] _ in
                self?.updateChromeForSidebar()
            },
        ]
        applyViewPreferences()
        updateTitle()
        updateStatus()
        sidebar.reloadAll()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    deinit {
        observers.forEach(NotificationCenter.default.removeObserver)
    }

    var note: Note {
        noteDocument?.note ?? editor.note
    }

    /// Joins the frontmost note window as a new tab when first shown.
    override func showWindow(_ sender: Any?) {
        if let window, !window.isVisible, window.tabbedWindows == nil,
           let host = NoteDocuments.frontWindow, host !== window {
            host.addTabbedWindow(window, ordered: .above)
        }
        super.showWindow(sender)
        if let window, window.tabGroup?.isTabBarVisible == true {
            window.toggleTabBar(nil)
        }
        NotificationCenter.default.post(name: .noteTabsDidChange, object: self)
    }

    /// Hides the system tab bar, which the tab strip replaces. AppKit won't
    /// hide it while a window has several tabs, so its view is hidden instead
    /// and the content moves up into its place. Should a later macOS lay the
    /// titlebar out differently, the system bar simply shows again.
    private func hideSystemTabBar() {
        guard let frame = window?.contentView?.superview else { return }
        var height: CGFloat = 0
        for bar in Self.subviews(of: frame) where NSStringFromClass(type(of: bar)).contains("TabBar") && !(bar is NoteTabStrip) {
            height = max(height, bar.frame.height)
            bar.isHidden = true
        }
        let visible = window?.tabGroup?.isTabBarVisible == true
        editorController.tabBarOffset = visible ? height : 0
        sidebarPanel.tabBarOffset = visible ? height : 0
    }

    private static func subviews(of view: NSView) -> [NSView] {
        view.subviews + view.subviews.flatMap(subviews(of:))
    }

    /// Lists this window's tab group in the tab strip.
    private func updateTabs() {
        // Touching a window's tab group before it's shown can order it in on
        // its own, and then it no longer joins the front window as a tab.
        guard let window, window.isVisible else { return }
        DispatchQueue.main.async { [weak self] in self?.hideSystemTabBar() }
        let windows = window.tabbedWindows ?? [window]
        tabStrip.tabs = windows.map { tab in
            let controller = tab.windowController as? NoteWindowController
            return NoteTabStrip.Tab(window: tab, title: controller?.noteDocument?.displayName ?? tab.title,
                                    isEdited: tab.isDocumentEdited)
        }
        tabStrip.selected = window
        // The strip's width changed; keep it clear of the sidebar panel.
        updateChromeForSidebar()
    }

    private func applyViewPreferences() {
        let preferences = ViewPreferences.shared
        if editor.layoutMode != preferences.layoutMode { editor.layoutMode = preferences.layoutMode }
        if pageTheme != preferences.pageTheme {
            pageTheme = preferences.pageTheme
            editor.palette = pageTheme.palette
            window?.backgroundColor = pageTheme.palette.canvas
            sidebar.pageThemeDidChange()
        }
        if editor.showsMarkdownSyntax != preferences.showsMarkdownSyntax {
            editor.showsMarkdownSyntax = preferences.showsMarkdownSyntax
        }
        editor.showsCoverAndIcon = preferences.showsCoversAndIcons
        lensButton.isOn = preferences.showsMarkdownSyntax
        updateStatus()
    }

    /// "1,284 words · Rendered", in the pill at the bottom right.
    private func updateStatus() {
        let words = note.wordCount
        let count = words == 1 ? "1 word" : "\(words.formatted()) words"
        editorController.status = count + " · " + (ViewPreferences.shared.showsMarkdownSyntax ? "Markdown" : "Rendered")
    }

    private func documentChanged(origin: NoteDocument.ChangeOrigin?) {
        guard let note = noteDocument?.note else { return }
        if origin != .editor {
            editor.setNote(note, preservingSelection: true)
        }
        visiblePage = min(visiblePage, note.pages.count)
        updateTitle()
        updateStatus()
        sidebar.noteDidChange()
    }

    private func visiblePageChanged(_ page: Int) {
        guard page != visiblePage else { return }
        visiblePage = page
        updateTitle()
        sidebar.visiblePageDidChange()
    }

    func scrollToPage(_ page: Int) {
        editor.scrollToPage(page)
        visiblePageChanged(page)
    }

    func focusEditor() {
        window?.makeFirstResponder(editor)
    }

    /// The folder ⌘N creates notes in: the one selected in the sidebar, or the current note's.
    var selectedFolder: URL? {
        sidebar.selectedFolder
    }

    /// The note's folder as the tab strip's tooltip; its name shows in the strip.
    private func updateTitle() {
        let folders = noteDocument?.fileURL.flatMap(AppServices.shared.library.folder.relativeFolder(of:)) ?? ""
        tabStrip.toolTip = (["Notes"] + folders.split(separator: "/").map(String.init)).joined(separator: " / ")
        NotificationCenter.default.post(name: .noteTabsDidChange, object: self)
    }

    override func setDocumentEdited(_ dirtyFlag: Bool) {
        super.setDocumentEdited(dirtyFlag)
        NotificationCenter.default.post(name: .noteTabsDidChange, object: self)
    }

    override func synchronizeWindowTitleWithDocumentName() {
        super.synchronizeWindowTitleWithDocumentName()
        updateTitle()
    }

    // MARK: Actions

    @IBAction func toggleMarkdownLens(_ sender: Any?) {
        ViewPreferences.shared.showsMarkdownSyntax.toggle()
    }

    @IBAction func showMoreMenu(_ sender: Any?) {
        let menu = Self.moreMenu()
        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: moreButton.bounds.height + 6), in: moreButton)
    }

    private func showTitleMenu(from pill: NSView) {
        let menu = NSMenu()
        menu.addItem(withTitle: "Rename…", action: #selector(NSDocument.rename(_:)), keyEquivalent: "")
        menu.addItem(withTitle: "Move To…", action: #selector(NSDocument.move(_:)), keyEquivalent: "")
        menu.addItem(.separator())
        menu.addItem(withTitle: "Show in Finder", action: #selector(NoteDocument.showInFinder(_:)), keyEquivalent: "")
        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: pill.bounds.height + 6), in: pill)
    }

    /// Moves the tab strip to just past the sidebar panel while it's open.
    private func updateChromeForSidebar() {
        let open = split.splitViewItems.first.map { !$0.isCollapsed } ?? false
        sidebarButton.isOn = open
        DispatchQueue.main.async { [weak self] in
            guard let self, self.window?.isVisible == true else { return }
            guard open, let panel = self.split.splitViewItems.first?.viewController.view else {
                self.titleSpacerWidth.constant = 0
                return
            }
            // Measure with the toolbar laid out, so repeated calls give the same width.
            self.window?.contentView?.superview?.layoutSubtreeIfNeeded()
            let target = panel.convert(panel.bounds, to: nil).maxX + 12
            let withoutSpacer = self.tabStrip.convert(self.tabStrip.bounds, to: nil).minX - self.titleSpacerWidth.constant
            self.titleSpacerWidth.constant = max(0, (target - withoutSpacer).rounded())
        }
    }

    @IBAction func addPage(_ sender: Any?) {
        editor.addPage()
    }

    @IBAction func insertDrawing(_ sender: Any?) {
        editor.insertDrawingAtSelection()
    }

    @IBAction func insertFlashcards(_ sender: Any?) {
        editor.insertDeckAtSelection()
    }

    /// A Format menu item; its `representedObject` is an `EditorCommand` raw value.
    @IBAction func performEditorCommand(_ sender: Any?) {
        guard let raw = (sender as? NSMenuItem)?.representedObject as? String,
              let command = EditorCommand(rawValue: raw) else { return }
        editor.perform(command)
    }

    /// Studies this note's deck, or offers every deck when it has several or none.
    @IBAction func studyFlashcards(_ sender: Any?) {
        let decks = note.decks.filter { !$0.cards.isEmpty }
        if decks.count == 1 {
            study(decks[0])
        } else {
            CommandPalette.shared.showDecks(over: window)
        }
    }

    /// ⇧⌘N: a folder inside the selected one, named in place in the sidebar.
    @IBAction func newFolder(_ sender: Any?) {
        sidebar.newFolder(in: selectedFolder)
    }

    /// ⌘T and the tab bar's + button: a new note in a new tab of this window.
    @IBAction override func newWindowForTab(_ sender: Any?) {
        (NSApp.delegate as? AppDelegate)?.newNote(sender)
    }

    private func study(_ deck: CardDeck) {
        guard let url = noteDocument?.fileURL else { return }
        FlashcardStudyWindowController.show(note: url, deckID: deck.id)
    }

    func validateMenuItem(_ item: NSMenuItem) -> Bool {
        if item.action == #selector(performEditorCommand(_:)) {
            return (item.representedObject as? String).flatMap(EditorCommand.init(rawValue:)) != nil
        }
        return true
    }

    // MARK: NSWindowDelegate

    func windowDidBecomeMain(_ notification: Notification) {
        sidebar.reloadAll()
        updateChromeForSidebar()
    }

    func windowDidResize(_ notification: Notification) {
        updateChromeForSidebar()
        hideSystemTabBar()
    }

    func windowDidBecomeKey(_ notification: Notification) {
        hideSystemTabBar()
        tabStrip.selectionDidAppear()
    }

    // MARK: Touch Bar

    override func makeTouchBar() -> NSTouchBar? {
        touchBarProvider.makeTouchBar()
    }

    // MARK: Toolbar

    private static let sidebarID = NSToolbarItem.Identifier("sidebar")
    private static let spacerID = NSToolbarItem.Identifier("titleSpacer")
    private static let titleID = NSToolbarItem.Identifier("title")
    private static let lensID = NSToolbarItem.Identifier("lens")

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [Self.sidebarID, Self.spacerID, Self.titleID, .flexibleSpace, Self.lensID]
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        toolbarDefaultItemIdentifiers(toolbar)
    }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier id: NSToolbarItem.Identifier, willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        let item = NSToolbarItem(itemIdentifier: id)
        switch id {
        case Self.sidebarID:
            item.view = GlassView.wrapping([sidebarButton])
            item.label = "Sidebar"
        case Self.spacerID:
            item.view = titleSpacer
            item.label = ""
        case Self.titleID:
            item.view = tabStrip
            item.label = "Open Notes"
        case Self.lensID:
            item.view = GlassView.wrapping([lensButton, moreButton])
            item.label = "Markdown and More"
        default:
            return nil
        }
        return item
    }

    private static func moreMenu() -> NSMenu {
        let menu = NSMenu()
        let export = NSMenu(title: "Export")
        export.addItem(withTitle: "Markdown…", action: #selector(NoteDocument.exportMarkdown(_:)), keyEquivalent: "")
        export.addItem(withTitle: "PDF (Blueprint)…", action: #selector(NoteDocument.exportBlueprintPDF(_:)), keyEquivalent: "")
        export.addItem(withTitle: "PDF (Print)…", action: #selector(NoteDocument.exportPrintPDF(_:)), keyEquivalent: "")
        menu.addItem(withTitle: "Export", action: nil, keyEquivalent: "").submenu = export
        menu.addItem(.separator())
        menu.addItem(withTitle: "Add Page", action: #selector(addPage(_:)), keyEquivalent: "")
        menu.addItem(withTitle: "Insert Drawing", action: #selector(insertDrawing(_:)), keyEquivalent: "")
        menu.addItem(withTitle: "Insert Flashcards", action: #selector(insertFlashcards(_:)), keyEquivalent: "")
        menu.addItem(withTitle: "Study Flashcards…", action: #selector(studyFlashcards(_:)), keyEquivalent: "")
        menu.addItem(.separator())
        menu.addItem(withTitle: "Show in Finder", action: #selector(NoteDocument.showInFinder(_:)), keyEquivalent: "")
        return menu
    }
}

/// Hosts the editor, edge to edge below the titlebar, with the word count
/// pill floating at the bottom right.
final class EditorViewController: NSViewController {
    private let editor: NoteEditorView
    private let statusLabel = NSTextField(labelWithString: "")
    private var top: NSLayoutConstraint?

    /// The height of the hidden system tab bar, which the editor moves up into.
    var tabBarOffset: CGFloat = 0 {
        didSet { top?.constant = -tabBarOffset }
    }

    var status: String {
        get { statusLabel.stringValue }
        set { statusLabel.stringValue = newValue }
    }

    init(editor: NoteEditorView) {
        self.editor = editor
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override func loadView() {
        let container = NSView()
        editor.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(editor)
        NSLayoutConstraint.activate([
            editor.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            editor.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            editor.bottomAnchor.constraint(equalTo: container.bottomAnchor),
        ])
        statusLabel.font = .systemFont(ofSize: 11.5)
        statusLabel.textColor = .secondaryLabelColor
        let pill = GlassView()
        pill.translatesAutoresizingMaskIntoConstraints = false
        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        pill.addSubview(statusLabel)
        container.addSubview(pill)
        NSLayoutConstraint.activate([
            statusLabel.leadingAnchor.constraint(equalTo: pill.leadingAnchor, constant: 12),
            statusLabel.trailingAnchor.constraint(equalTo: pill.trailingAnchor, constant: -12),
            statusLabel.centerYAnchor.constraint(equalTo: pill.centerYAnchor),
            pill.heightAnchor.constraint(equalToConstant: 28),
            pill.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -16),
            pill.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -14),
        ])
        top = editor.topAnchor.constraint(equalTo: container.safeAreaLayoutGuide.topAnchor, constant: -tabBarOffset)
        top?.isActive = true
        view = container
    }
}
