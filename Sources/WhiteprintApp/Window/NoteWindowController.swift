import AppKit
import WhiteprintCore
import WhiteprintEditor

/// One note's window: sidebar, slim breadcrumb toolbar and the editor. Note
/// windows open as tabs of the frontmost note window.
final class NoteWindowController: NSWindowController, NSWindowDelegate, NSToolbarDelegate, NSMenuItemValidation {
    static let tabbingIdentifier = "io.github.l1203012.whiteprint.note"
    /// The tab bar is shown once per launch; after that it's the user's to hide.
    private static var hasShownTabBar = false

    private(set) weak var noteDocument: NoteDocument?
    private let editor: NoteEditorView
    private let sidebar: SidebarViewController
    private let breadcrumb = NSTextField(labelWithString: "")
    private var observers: [NSObjectProtocol] = []
    private let touchBarProvider = NoteTouchBar()
    private(set) var visiblePage = 1
    private var pageTheme = ViewPreferences.shared.pageTheme

    init(document: NoteDocument) {
        noteDocument = document
        editor = NoteEditorView(note: document.note, palette: pageTheme.palette)
        sidebar = SidebarViewController()

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1080, height: 760),
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered, defer: true
        )
        window.minSize = NSSize(width: 560, height: 420)
        window.titleVisibility = .hidden
        window.toolbarStyle = .unified
        window.tabbingMode = .preferred
        window.tabbingIdentifier = Self.tabbingIdentifier

        let split = NSSplitViewController()
        let sidebarItem = NSSplitViewItem(sidebarWithViewController: sidebar)
        sidebarItem.minimumThickness = 200
        sidebarItem.maximumThickness = 340
        sidebarItem.canCollapse = true
        split.addSplitViewItem(sidebarItem)
        let contentItem = NSSplitViewItem(viewController: EditorViewController(editor: editor))
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

        breadcrumb.lineBreakMode = .byTruncatingMiddle
        breadcrumb.widthAnchor.constraint(lessThanOrEqualToConstant: 520).isActive = true

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
                self?.updateBreadcrumb()
            },
            center.addObserver(forName: .viewPreferencesDidChange, object: nil, queue: .main) { [weak self] _ in
                self?.applyViewPreferences()
            },
        ]
        applyViewPreferences()
        updateBreadcrumb()
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
        if !Self.hasShownTabBar, let window, let group = window.tabGroup {
            Self.hasShownTabBar = true
            if !group.isTabBarVisible { window.toggleTabBar(nil) }
        }
    }

    private func applyViewPreferences() {
        let preferences = ViewPreferences.shared
        if editor.layoutMode != preferences.layoutMode { editor.layoutMode = preferences.layoutMode }
        if pageTheme != preferences.pageTheme {
            pageTheme = preferences.pageTheme
            editor.palette = pageTheme.palette
            sidebar.pageThemeDidChange()
        }
        if editor.showsMarkdownSyntax != preferences.showsMarkdownSyntax {
            editor.showsMarkdownSyntax = preferences.showsMarkdownSyntax
        }
        editor.showsCoverAndIcon = preferences.showsCoversAndIcons
    }

    private func documentChanged(origin: NoteDocument.ChangeOrigin?) {
        guard let note = noteDocument?.note else { return }
        if origin != .editor {
            editor.setNote(note, preservingSelection: true)
        }
        visiblePage = min(visiblePage, note.pages.count)
        updateBreadcrumb()
        sidebar.noteDidChange()
    }

    private func visiblePageChanged(_ page: Int) {
        guard page != visiblePage else { return }
        visiblePage = page
        updateBreadcrumb()
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

    private func updateBreadcrumb() {
        let muted: [NSAttributedString.Key: Any] = [
            .foregroundColor: NSColor.secondaryLabelColor,
            .font: NSFont.systemFont(ofSize: 13),
        ]
        let strong: [NSAttributedString.Key: Any] = [
            .foregroundColor: NSColor.labelColor,
            .font: NSFont.systemFont(ofSize: 13, weight: .medium),
        ]
        let folders = noteDocument?.fileURL.flatMap(AppServices.shared.library.folder.relativeFolder(of:)) ?? ""
        let trail = (["Notes"] + folders.split(separator: "/").map(String.init)).joined(separator: "  /  ")
        let text = NSMutableAttributedString(string: trail + "  /  ", attributes: muted)
        text.append(NSAttributedString(string: noteDocument?.title ?? NoteTitle.untitled, attributes: strong))
        if note.pages.count > 1 {
            text.append(NSAttributedString(string: "  /  Page \(visiblePage)", attributes: muted))
        }
        breadcrumb.attributedStringValue = text
    }

    override func synchronizeWindowTitleWithDocumentName() {
        super.synchronizeWindowTitleWithDocumentName()
        updateBreadcrumb()
    }

    // MARK: Actions

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
    }

    // MARK: Touch Bar

    override func makeTouchBar() -> NSTouchBar? {
        touchBarProvider.makeTouchBar()
    }

    // MARK: Toolbar

    private static let breadcrumbID = NSToolbarItem.Identifier("breadcrumb")
    private static let moreID = NSToolbarItem.Identifier("more")

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.toggleSidebar, .sidebarTrackingSeparator, Self.breadcrumbID, .flexibleSpace, Self.moreID]
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        toolbarDefaultItemIdentifiers(toolbar)
    }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier id: NSToolbarItem.Identifier, willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        switch id {
        case Self.breadcrumbID:
            let item = NSToolbarItem(itemIdentifier: id)
            item.view = breadcrumb
            item.label = "Location"
            return item
        case Self.moreID:
            let item = NSMenuToolbarItem(itemIdentifier: id)
            item.image = NSImage(systemSymbolName: "ellipsis.circle", accessibilityDescription: "More")
            item.label = "More"
            item.toolTip = "Export, pages and more"
            item.showsIndicator = false
            item.menu = Self.moreMenu()
            return item
        default:
            return nil
        }
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

/// Hosts the editor, edge to edge, under the transparent toolbar.
final class EditorViewController: NSViewController {
    private let editor: NoteEditorView

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
            editor.topAnchor.constraint(equalTo: container.safeAreaLayoutGuide.topAnchor),
            editor.bottomAnchor.constraint(equalTo: container.bottomAnchor),
        ])
        view = container
    }
}
