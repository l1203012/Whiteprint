import AppKit
import WhiteprintCore
import WhiteprintEditor

/// One note's window: sidebar, slim breadcrumb toolbar and the editor.
final class NoteWindowController: NSWindowController, NSWindowDelegate, NSToolbarDelegate {
    private(set) weak var noteDocument: NoteDocument?
    private let editor: NoteEditorView
    private let sidebar: SidebarViewController
    private let breadcrumb = NSTextField(labelWithString: "")
    private var observer: NSObjectProtocol?
    private(set) var visiblePage = 1

    init(document: NoteDocument) {
        noteDocument = document
        editor = NoteEditorView(note: document.note)
        sidebar = SidebarViewController()

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1080, height: 760),
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered, defer: true
        )
        window.minSize = NSSize(width: 560, height: 420)
        window.titleVisibility = .hidden
        window.toolbarStyle = .unified
        window.tabbingMode = .disallowed

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
        observer = NotificationCenter.default.addObserver(forName: .noteDocumentDidChange, object: document, queue: .main) { [weak self] notification in
            self?.documentChanged(origin: notification.userInfo?["origin"] as? NoteDocument.ChangeOrigin)
        }
        updateBreadcrumb()
        sidebar.reloadAll()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    deinit {
        observer.map(NotificationCenter.default.removeObserver)
    }

    var note: Note {
        noteDocument?.note ?? editor.note
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

    private func updateBreadcrumb() {
        let muted: [NSAttributedString.Key: Any] = [
            .foregroundColor: NSColor.secondaryLabelColor,
            .font: NSFont.systemFont(ofSize: 13),
        ]
        let strong: [NSAttributedString.Key: Any] = [
            .foregroundColor: NSColor.labelColor,
            .font: NSFont.systemFont(ofSize: 13, weight: .medium),
        ]
        let text = NSMutableAttributedString(string: "Notes  /  ", attributes: muted)
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

    // MARK: NSWindowDelegate

    func windowDidBecomeMain(_ notification: Notification) {
        sidebar.reloadAll()
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
