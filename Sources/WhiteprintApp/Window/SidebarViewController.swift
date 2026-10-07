import AppKit
import WhiteprintCore
import WhiteprintRender
import WhiteprintStudy

/// The Notion-style source list: search, the notes folder tree, the current
/// note's pages, study material and flashcard decks, and "New note".
final class SidebarViewController: NSViewController, NSOutlineViewDataSource, NSOutlineViewDelegate, NSMenuDelegate, NSTextFieldDelegate {
    weak var windowController: NoteWindowController?

    private let outline = NSOutlineView()
    private let notes = SidebarNode(.section("Notes"))
    private let pages = SidebarNode(.section("Pages"))
    private let study = SidebarNode(.section("Study"))
    private var thumbnails: [Int: (page: NotePage, image: NSImage)] = [:]
    private var observers: [NSObjectProtocol] = []
    private var pendingPageReload = false
    /// A folder the user selected; otherwise the current note is selected.
    private var selectedFolderPath: String?
    private var renaming: (url: URL, field: NSTextField)?
    private var isRestoringExpansion = false

    private static let thumbnailSize = NSSize(width: 22, height: 30)
    static let expandedFoldersKey = "SidebarExpandedFolders"

    deinit {
        observers.forEach(NotificationCenter.default.removeObserver)
    }

    override func loadView() {
        let search = SearchButton(target: self, action: #selector(showPalette(_:)))

        let column = NSTableColumn(identifier: .init("main"))
        outline.addTableColumn(column)
        outline.outlineTableColumn = column
        outline.headerView = nil
        outline.style = .sourceList
        outline.floatsGroupRows = false
        outline.rowSizeStyle = .default
        outline.indentationPerLevel = 12
        outline.dataSource = self
        outline.delegate = self
        outline.target = self
        outline.action = #selector(rowClicked(_:))
        outline.doubleAction = #selector(rowDoubleClicked(_:))
        outline.autosaveExpandedItems = false
        outline.registerForDraggedTypes([.fileURL])
        outline.setDraggingSourceOperationMask(.move, forLocal: true)
        outline.setDraggingSourceOperationMask(.copy, forLocal: false)
        let menu = NSMenu()
        menu.delegate = self
        outline.menu = menu

        let scroll = NSScrollView()
        scroll.documentView = outline
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        scroll.automaticallyAdjustsContentInsets = false

        let newNote = NSButton(title: "New note", image: NSImage(systemSymbolName: "plus", accessibilityDescription: nil)!, target: nil, action: #selector(AppDelegate.newNote(_:)))
        newNote.isBordered = false
        newNote.imagePosition = .imageLeading
        newNote.contentTintColor = .secondaryLabelColor
        newNote.font = .systemFont(ofSize: 13)

        let root = NSView()
        for view in [search, scroll, newNote] as [NSView] {
            view.translatesAutoresizingMaskIntoConstraints = false
            root.addSubview(view)
        }
        NSLayoutConstraint.activate([
            search.topAnchor.constraint(equalTo: root.safeAreaLayoutGuide.topAnchor, constant: 8),
            search.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 10),
            search.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -10),
            scroll.topAnchor.constraint(equalTo: search.bottomAnchor, constant: 6),
            scroll.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            scroll.bottomAnchor.constraint(equalTo: newNote.topAnchor, constant: -6),
            newNote.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 16),
            newNote.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -12),
        ])
        view = root
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        let center = NotificationCenter.default
        observers = [
            center.addObserver(forName: .notesLibraryDidChange, object: nil, queue: .main) { [weak self] _ in self?.reloadNotes() },
            center.addObserver(forName: .studyStoreDidChange, object: nil, queue: .main) { [weak self] _ in self?.reloadStudy() },
            center.addObserver(forName: .studySessionDidChange, object: nil, queue: .main) { [weak self] _ in self?.reloadStudy() },
            center.addObserver(forName: .flashcardProgressDidChange, object: nil, queue: .main) { [weak self] _ in self?.reloadStudy() },
        ]
        reloadAll()
        [notes, pages, study].forEach { outline.expandItem($0) }
        restoreExpansion(revealingCurrentNote: true)
        selectCurrentNote()
    }

    // MARK: Content

    func reloadAll() {
        guard isViewLoaded, renaming == nil else { return }
        rebuildNotes()
        rebuildPages()
        rebuildStudy()
        outline.reloadData()
        restoreExpansion(revealingCurrentNote: true)
        selectCurrentNote()
    }

    func noteDidChange() {
        // Typing reports changes every few hundred milliseconds; thumbnails can lag a little.
        guard !pendingPageReload else { return }
        pendingPageReload = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) { [weak self] in
            guard let self else { return }
            self.pendingPageReload = false
            self.rebuildPages()
            self.outline.reloadItem(self.pages, reloadChildren: true)
            self.selectCurrentNote()
        }
    }

    /// Redraws the page thumbnails in the new theme's colours.
    func pageThemeDidChange() {
        guard isViewLoaded else { return }
        thumbnails = [:]
        outline.reloadItem(pages, reloadChildren: true)
        selectCurrentNote()
    }

    func visiblePageDidChange() {
        guard isViewLoaded else { return }
        outline.reloadItem(pages, reloadChildren: true)
        selectCurrentNote()
    }

    private func reloadNotes() {
        guard renaming == nil else { return }
        rebuildNotes()
        outline.reloadItem(notes, reloadChildren: true)
        restoreExpansion(revealingCurrentNote: false)
        selectCurrentNote()
        reloadStudy()
    }

    private func reloadStudy() {
        rebuildStudy()
        outline.reloadItem(study, reloadChildren: true)
    }

    private func rebuildNotes() {
        let library = AppServices.shared.library
        let tree = NoteTree.build(entries: library.entries, folders: library.folders)
        notes.children = Self.nodes(for: tree, root: library.folder.url)
    }

    private static func nodes(for tree: NoteTree, root: URL) -> [SidebarNode] {
        let folders = tree.folders.map { folder -> SidebarNode in
            let node = SidebarNode(.folder(path: folder.path, name: folder.name, url: root.appendingPathComponent(folder.path, isDirectory: true)))
            node.children = nodes(for: folder, root: root)
            return node
        }
        return folders + tree.notes.map { SidebarNode(.note($0)) }
    }

    private func rebuildPages() {
        guard let note = windowController?.note else { return }
        let titles = PageOutline.titles(of: note)
        pages.children = titles.indices.map { SidebarNode(.page($0 + 1, titles[$0])) }
        thumbnails = thumbnails.filter { $0.key <= note.pages.count }
    }

    private func rebuildStudy() {
        let session = AppServices.shared.study
        let decks = DeckCatalog.all().map { SidebarNode(.deck($0)) }
        study.children = session.imports.map { SidebarNode(.studyImport($0)) }
            + session.extracting.map { SidebarNode(.extracting($0)) }
            + [SidebarNode(session.imports.isEmpty ? .addMaterial : .generate)]
            + decks
    }

    private var currentURL: URL? {
        windowController?.noteDocument?.fileURL?.canonicalFile
    }

    private func selectCurrentNote() {
        let target = selectedFolderPath.flatMap { path in node(where: { $0.folderPath == path }) }
            ?? currentURL.flatMap { url in node(where: { $0.noteURL == url }) }
        let row = target.map(outline.row(forItem:)) ?? -1
        if row >= 0 {
            outline.selectRowIndexes([row], byExtendingSelection: false)
        } else {
            outline.deselectAll(nil)
        }
    }

    private func node(where matches: (SidebarNode) -> Bool) -> SidebarNode? {
        func search(_ nodes: [SidebarNode]) -> SidebarNode? {
            for node in nodes {
                if matches(node) { return node }
                if let found = search(node.children) { return found }
            }
            return nil
        }
        return search(notes.children)
    }

    /// The folder new notes and folders go into: the selected folder, or the
    /// selected note's folder when it's inside the notes folder.
    var selectedFolder: URL? {
        guard isViewLoaded, let node = outline.item(atRow: outline.selectedRow) as? SidebarNode else { return nil }
        switch node.kind {
        case .folder(_, _, let url):
            return url
        case .note(let entry):
            return entry.folder == nil ? nil : entry.url.deletingLastPathComponent()
        default:
            return nil
        }
    }

    private func thumbnail(forPage number: Int) -> NSImage? {
        guard let note = windowController?.note, number <= note.pages.count else { return nil }
        let page = note.pages[number - 1]
        if let cached = thumbnails[number], cached.page == page { return cached.image }
        let image = PageThumbnail.image(for: page, size: Self.thumbnailSize, palette: ViewPreferences.shared.pageTheme.palette)
        thumbnails[number] = (page, image)
        return image
    }

    // MARK: Expansion

    private var expandedFolders: Set<String> {
        get { Set(AppDefaults.store.stringArray(forKey: Self.expandedFoldersKey) ?? []) }
        set { AppDefaults.store.set(newValue.sorted(), forKey: Self.expandedFoldersKey) }
    }

    /// Re-expands remembered folders after a reload, and with
    /// `revealingCurrentNote` the folders holding the current note.
    private func restoreExpansion(revealingCurrentNote: Bool) {
        var paths = expandedFolders
        if revealingCurrentNote, let url = currentURL,
           let folder = AppServices.shared.library.folder.relativeFolder(of: url), !folder.isEmpty {
            let parts = folder.split(separator: "/")
            for i in parts.indices { paths.insert(parts[...i].joined(separator: "/")) }
            expandedFolders = paths
        }
        isRestoringExpansion = true
        defer { isRestoringExpansion = false }
        func expand(_ nodes: [SidebarNode]) {
            for node in nodes {
                guard let path = node.folderPath, paths.contains(path) else { continue }
                outline.expandItem(node)
                expand(node.children)
            }
        }
        expand(notes.children)
    }

    func outlineViewItemDidExpand(_ notification: Notification) {
        guard !isRestoringExpansion, let path = (notification.userInfo?["NSObject"] as? SidebarNode)?.folderPath else { return }
        expandedFolders.insert(path)
    }

    func outlineViewItemDidCollapse(_ notification: Notification) {
        guard !isRestoringExpansion, let path = (notification.userInfo?["NSObject"] as? SidebarNode)?.folderPath else { return }
        expandedFolders = expandedFolders.filter { $0 != path && !$0.hasPrefix(path + "/") }
    }

    // MARK: Actions

    @objc private func showPalette(_ sender: Any?) {
        CommandPalette.shared.show(over: view.window)
    }

    @objc private func rowClicked(_ sender: Any?) {
        guard let node = outline.item(atRow: outline.clickedRow) as? SidebarNode else { return }
        switch node.kind {
        case .folder(let path, _, _):
            selectedFolderPath = path
        case .note(let entry):
            selectedFolderPath = nil
            if entry.url != currentURL {
                NoteDocuments.open(entry.url)
            }
        case .page(let number, _):
            windowController?.scrollToPage(number)
        case .studyImport, .extracting, .addMaterial:
            StudyPanelController.present(over: view.window)
        case .generate:
            StudyPanelController.present(over: view.window, generating: true)
        case .deck(let deck):
            FlashcardStudyWindowController.show(note: deck.note, deckID: deck.deck.id)
        case .section:
            break
        }
        selectCurrentNote()
    }

    @objc private func rowDoubleClicked(_ sender: Any?) {
        guard let node = outline.item(atRow: outline.clickedRow) as? SidebarNode, node.folderPath != nil else { return }
        if outline.isItemExpanded(node) {
            outline.collapseItem(node)
        } else {
            outline.expandItem(node)
        }
    }

    /// Creates a folder in `parent` (default: the top level) and starts naming it.
    func newFolder(in parent: URL?) {
        guard let app = NSApp.delegate as? AppDelegate, let url = app.createFolder(in: parent) else { return }
        let library = AppServices.shared.library
        if let parent, let path = library.folder.relativePath(of: parent), !path.isEmpty {
            var paths = expandedFolders
            paths.insert(path)
            expandedFolders = paths
        }
        reloadNotes()
        let path = library.folder.relativePath(of: url)
        guard let node = node(where: { $0.folderPath == path }) else { return }
        selectedFolderPath = path
        selectCurrentNote()
        beginRename(node)
    }

    // MARK: Context menu

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        let node = outline.item(atRow: outline.clickedRow) as? SidebarNode
        let root = AppServices.shared.library.folder.url
        func add(_ title: String, _ action: Selector, _ object: Any?) {
            let item = NSMenuItem(title: title, action: action, keyEquivalent: "")
            item.target = self
            item.representedObject = object
            menu.addItem(item)
        }
        switch node?.kind {
        case .folder(_, _, let url):
            add("New Note in Folder", #selector(contextNewNote(_:)), url)
            add("New Folder", #selector(contextNewFolder(_:)), url)
            menu.addItem(.separator())
            add("Rename", #selector(contextRename(_:)), node)
            add("Show in Finder", #selector(contextShowInFinder(_:)), url)
            menu.addItem(.separator())
            add("Move to Trash…", #selector(contextTrash(_:)), url)
        case .note(let entry):
            add("New Note", #selector(contextNewNote(_:)), entry.folder == nil ? root : entry.url.deletingLastPathComponent())
            menu.addItem(.separator())
            if entry.folder != nil { add("Rename", #selector(contextRename(_:)), node) }
            add("Show in Finder", #selector(contextShowInFinder(_:)), entry.url)
            if entry.folder != nil {
                menu.addItem(.separator())
                add("Move to Trash…", #selector(contextTrash(_:)), entry.url)
            }
        case .deck(let deck):
            add("Study", #selector(contextStudy(_:)), node)
            add("Open Note", #selector(contextOpen(_:)), deck.note)
        default:
            add("New Note", #selector(contextNewNote(_:)), root)
            add("New Folder", #selector(contextNewFolder(_:)), root)
        }
    }

    @objc private func contextNewNote(_ sender: NSMenuItem) {
        (NSApp.delegate as? AppDelegate)?.newNote(in: sender.representedObject as? URL)
    }

    @objc private func contextNewFolder(_ sender: NSMenuItem) {
        newFolder(in: sender.representedObject as? URL)
    }

    @objc private func contextRename(_ sender: NSMenuItem) {
        (sender.representedObject as? SidebarNode).map(beginRename)
    }

    @objc private func contextShowInFinder(_ sender: NSMenuItem) {
        (sender.representedObject as? URL).map { NSWorkspace.shared.activateFileViewerSelecting([$0]) }
    }

    @objc private func contextOpen(_ sender: NSMenuItem) {
        (sender.representedObject as? URL).map(NoteDocuments.open)
    }

    @objc private func contextStudy(_ sender: NSMenuItem) {
        guard case .deck(let deck) = (sender.representedObject as? SidebarNode)?.kind else { return }
        FlashcardStudyWindowController.show(note: deck.note, deckID: deck.deck.id)
    }

    @objc private func contextTrash(_ sender: NSMenuItem) {
        guard let url = sender.representedObject as? URL, let window = view.window else { return }
        let isFolder = url.hasDirectoryPath
        let alert = NSAlert()
        alert.messageText = "Move “\(isFolder ? url.lastPathComponent : NoteTitle.baseName(url))” to the Trash?"
        if isFolder {
            let count = AppServices.shared.library.entries.filter { NoteFiles.isInside($0.url, url) }.count
            alert.informativeText = count == 0
                ? "The folder is empty."
                : "The folder and the \(count) note\(count == 1 ? "" : "s") in it go to the Trash. Open notes are closed."
        } else {
            alert.informativeText = "You can put it back from the Trash in Finder."
        }
        alert.addButton(withTitle: "Move to Trash")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { response in
            guard response == .alertFirstButtonReturn else { return }
            do {
                try NoteFileActions.trash(url)
            } catch {
                NSApp.presentError(error)
            }
        }
    }

    // MARK: Rename

    private func beginRename(_ node: SidebarNode) {
        let row = outline.row(forItem: node)
        guard row >= 0, let url = node.fileURL,
              let field = (outline.view(atColumn: 0, row: row, makeIfNecessary: true) as? NSTableCellView)?.textField else { return }
        renaming = (url, field)
        field.isEditable = true
        field.delegate = self
        if case .note = node.kind { field.stringValue = NoteTitle.baseName(url) }
        view.window?.makeFirstResponder(field)
        field.currentEditor()?.selectAll(nil)
    }

    func controlTextDidEndEditing(_ notification: Notification) {
        guard let (url, field) = renaming, notification.object as? NSTextField === field else { return }
        renaming = nil
        field.isEditable = false
        let cancelled = (notification.userInfo?["NSTextMovement"] as? Int) == NSTextMovement.cancel.rawValue
        let name = field.stringValue.trimmingCharacters(in: .whitespaces)
        if !cancelled, !name.isEmpty {
            do {
                let renamed = try NoteFileActions.rename(url, to: name)
                if url.hasDirectoryPath, selectedFolderPath != nil {
                    selectedFolderPath = AppServices.shared.library.folder.relativePath(of: renamed)
                }
                var paths = expandedFolders
                if let old = AppServices.shared.library.folder.relativePath(of: url), paths.contains(old),
                   let new = AppServices.shared.library.folder.relativePath(of: renamed) {
                    paths.remove(old)
                    paths.insert(new)
                    expandedFolders = paths
                }
            } catch {
                NSApp.presentError(error)
            }
        }
        reloadNotes()
        view.window?.makeFirstResponder(outline)
    }

    func control(_ control: NSControl, textView: NSTextView, doCommandBy selector: Selector) -> Bool {
        guard selector == #selector(NSResponder.cancelOperation(_:)), let field = renaming?.field, control === field else { return false }
        field.abortEditing()
        controlTextDidEndEditing(Notification(name: NSControl.textDidEndEditingNotification, object: field,
                                              userInfo: ["NSTextMovement": NSTextMovement.cancel.rawValue]))
        return true
    }

    // MARK: Drag and drop

    func outlineView(_ outlineView: NSOutlineView, pasteboardWriterForItem item: Any) -> NSPasteboardWriting? {
        guard renaming == nil, let node = item as? SidebarNode, let url = node.fileURL else { return nil }
        if case .note(let entry) = node.kind, entry.folder == nil { return nil }
        return url as NSURL
    }

    func outlineView(_ outlineView: NSOutlineView, validateDrop info: NSDraggingInfo, proposedItem item: Any?, proposedChildIndex index: Int) -> NSDragOperation {
        var target = item as? SidebarNode
        if case .note = target?.kind { target = outlineView.parent(forItem: target) as? SidebarNode }
        guard let target, let folder = dropFolder(target) else { return [] }
        let urls = draggedNotes(info)
        guard !urls.isEmpty, urls.allSatisfy({ !NoteFiles.isInside(folder, $0) && $0.deletingLastPathComponent().path != folder.path }) else { return [] }
        outlineView.setDropItem(target, dropChildIndex: NSOutlineViewDropOnItemIndex)
        return .move
    }

    func outlineView(_ outlineView: NSOutlineView, acceptDrop info: NSDraggingInfo, item: Any?, childIndex index: Int) -> Bool {
        guard let target = item as? SidebarNode, let folder = dropFolder(target) else { return false }
        if let path = target.folderPath {
            var paths = expandedFolders
            paths.insert(path)
            expandedFolders = paths
        }
        do {
            try NoteFileActions.move(draggedNotes(info), into: folder)
            return true
        } catch {
            NSApp.presentError(error)
            return false
        }
    }

    /// The folder a drop on `node` goes into: a folder, or the notes folder itself.
    private func dropFolder(_ node: SidebarNode) -> URL? {
        if case .folder(_, _, let url) = node.kind { return url.canonicalFile }
        return node === notes ? AppServices.shared.library.folder.url.canonicalFile : nil
    }

    /// Dragged files and folders from inside the notes folder.
    private func draggedNotes(_ info: NSDraggingInfo) -> [URL] {
        let urls = info.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        let folder = AppServices.shared.library.folder
        return urls.map(\.canonicalFile).filter(folder.contains)
    }

    // MARK: NSOutlineViewDataSource

    func outlineView(_ outlineView: NSOutlineView, numberOfChildrenOfItem item: Any?) -> Int {
        (item as? SidebarNode)?.children.count ?? 3
    }

    func outlineView(_ outlineView: NSOutlineView, child index: Int, ofItem item: Any?) -> Any {
        (item as? SidebarNode)?.children[index] ?? [notes, pages, study][index]
    }

    func outlineView(_ outlineView: NSOutlineView, isItemExpandable item: Any) -> Bool {
        guard let node = item as? SidebarNode else { return false }
        return node.isSection || node.folderPath != nil
    }

    // MARK: NSOutlineViewDelegate

    func outlineView(_ outlineView: NSOutlineView, isGroupItem item: Any) -> Bool {
        (item as? SidebarNode)?.isSection ?? false
    }

    func outlineView(_ outlineView: NSOutlineView, shouldSelectItem item: Any) -> Bool {
        switch (item as? SidebarNode)?.kind {
        case .note, .folder: return true
        default: return false
        }
    }

    func outlineView(_ outlineView: NSOutlineView, heightOfRowByItem item: Any) -> CGFloat {
        if case .page = (item as? SidebarNode)?.kind { return Self.thumbnailSize.height + 8 }
        return 26
    }

    func outlineView(_ outlineView: NSOutlineView, viewFor tableColumn: NSTableColumn?, item: Any) -> NSView? {
        guard let node = item as? SidebarNode else { return nil }
        let cell = SidebarCell()
        switch node.kind {
        case .section(let title):
            cell.configure(title: title, header: true)
        case .folder(_, let name, _):
            cell.configure(title: name, symbol: "folder")
        case .note(let entry):
            cell.configure(title: entry.title, symbol: "doc.text")
        case let .page(number, title):
            let current = number == windowController?.visiblePage
            cell.configure(title: title, image: thumbnail(forPage: number), detail: "\(number)", emphasized: current)
        case .studyImport(let item):
            cell.configure(title: item.name, symbol: "doc.richtext", detail: "\(item.chunksDone)/\(item.chunkCount)")
        case .extracting(let name):
            cell.configure(title: name, symbol: "hourglass", detail: "…")
        case .addMaterial:
            cell.configure(title: "Add course material…", symbol: "tray.and.arrow.down", muted: true)
        case .generate:
            cell.configure(title: "Generate study plan", symbol: "sparkles", muted: true)
        case .deck(let deck):
            let toStudy = deck.summary.due + deck.summary.new
            cell.configure(title: deck.title, symbol: "rectangle.on.rectangle.angled", detail: toStudy > 0 ? "\(toStudy)" : "✓")
            cell.toolTip = "\(deck.noteTitle) · \(deck.summary)"
        }
        return cell
    }
}

/// An outline row. Sections and folders own their rows.
final class SidebarNode {
    enum Kind {
        case section(String)
        case folder(path: String, name: String, url: URL)
        case note(NoteEntry)
        case page(Int, String)
        case studyImport(StudyImport)
        case extracting(String)
        case addMaterial
        case generate
        case deck(DeckCatalog.Entry)
    }

    let kind: Kind
    var children: [SidebarNode] = []

    init(_ kind: Kind) {
        self.kind = kind
    }

    var isSection: Bool {
        if case .section = kind { return true }
        return false
    }

    var folderPath: String? {
        if case .folder(let path, _, _) = kind { return path }
        return nil
    }

    var noteURL: URL? {
        if case .note(let entry) = kind { return entry.url }
        return nil
    }

    /// The file or folder the row stands for.
    var fileURL: URL? {
        switch kind {
        case .folder(_, _, let url): return url
        case .note(let entry): return entry.url
        default: return nil
        }
    }
}

/// Icon, title and an optional trailing detail.
private final class SidebarCell: NSTableCellView {
    private let title = NSTextField(labelWithString: "")
    private let detail = NSTextField(labelWithString: "")
    private let icon = NSImageView()

    init() {
        super.init(frame: .zero)
        textField = title
        imageView = icon
        title.lineBreakMode = .byTruncatingTail
        title.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        title.setContentHuggingPriority(.init(1), for: .horizontal)
        title.cell?.isScrollable = true
        detail.textColor = .tertiaryLabelColor
        detail.font = .monospacedDigitSystemFont(ofSize: 11, weight: .regular)
        detail.alignment = .right
        detail.setContentHuggingPriority(.required, for: .horizontal)
        icon.imageScaling = .scaleProportionallyDown
        let stack = NSStackView(views: [icon, title, detail])
        stack.spacing = 7
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 2),
            stack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -6),
            stack.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    func configure(
        title text: String, symbol: String? = nil, image: NSImage? = nil, detail detailText: String? = nil,
        header: Bool = false, emphasized: Bool = false, muted: Bool = false
    ) {
        title.stringValue = text
        detail.stringValue = detailText ?? ""
        detail.isHidden = detailText == nil
        if header {
            title.font = .systemFont(ofSize: 11, weight: .semibold)
            title.textColor = .tertiaryLabelColor
        } else {
            title.font = .systemFont(ofSize: 13, weight: emphasized ? .medium : .regular)
            title.textColor = muted ? .secondaryLabelColor : .labelColor
        }
        if let image {
            icon.image = image
            icon.contentTintColor = nil
        } else if let symbol {
            icon.image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)
            icon.contentTintColor = .secondaryLabelColor
        }
        icon.isHidden = icon.image == nil
    }
}

/// A search-field look-alike that opens the command palette.
private final class SearchButton: NSButton {
    convenience init(target: AnyObject, action: Selector) {
        self.init(frame: .zero)
        self.target = target
        self.action = action
        bezelStyle = .roundRect
        isBordered = false
        wantsLayer = true
        layer?.cornerRadius = 6
        title = ""
        let glass = NSImageView(image: NSImage(systemSymbolName: "magnifyingglass", accessibilityDescription: nil)!)
        glass.contentTintColor = .secondaryLabelColor
        let label = NSTextField(labelWithString: "Search")
        label.textColor = .secondaryLabelColor
        let shortcut = NSTextField(labelWithString: "⌘K")
        shortcut.textColor = .tertiaryLabelColor
        shortcut.font = .systemFont(ofSize: 11)
        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let stack = NSStackView(views: [glass, label, spacer, shortcut])
        stack.spacing = 6
        stack.edgeInsets = NSEdgeInsets(top: 0, left: 8, bottom: 0, right: 8)
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: trailingAnchor),
            stack.centerYAnchor.constraint(equalTo: centerYAnchor),
            heightAnchor.constraint(equalToConstant: 26),
        ])
        setAccessibilityLabel("Search notes and commands")
        toolTip = "Search notes and commands (⌘K)"
    }

    override func updateLayer() {
        layer?.backgroundColor = NSColor.quaternaryLabelColor.withAlphaComponent(0.12).cgColor
    }

    override var wantsUpdateLayer: Bool { true }
}
