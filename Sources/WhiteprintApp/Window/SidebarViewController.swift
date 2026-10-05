import AppKit
import WhiteprintCore
import WhiteprintRender
import WhiteprintStudy

/// The Notion-style source list: search, the notes folder, the current
/// note's pages, study material, and "New note".
final class SidebarViewController: NSViewController, NSOutlineViewDataSource, NSOutlineViewDelegate {
    weak var windowController: NoteWindowController?

    private let outline = NSOutlineView()
    private let notes = SidebarNode(.section("Notes"))
    private let pages = SidebarNode(.section("Pages"))
    private let study = SidebarNode(.section("Study"))
    private var thumbnails: [Int: (page: NotePage, image: NSImage)] = [:]
    private var observers: [NSObjectProtocol] = []
    private var pendingPageReload = false

    private static let thumbnailSize = NSSize(width: 22, height: 30)

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
        outline.indentationPerLevel = 0
        outline.dataSource = self
        outline.delegate = self
        outline.target = self
        outline.action = #selector(rowClicked(_:))
        outline.autosaveExpandedItems = false

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
        ]
        reloadAll()
        [notes, pages, study].forEach { outline.expandItem($0) }
    }

    // MARK: Content

    func reloadAll() {
        guard isViewLoaded else { return }
        rebuildNotes()
        rebuildPages()
        rebuildStudy()
        outline.reloadData()
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

    func visiblePageDidChange() {
        guard isViewLoaded else { return }
        outline.reloadItem(pages, reloadChildren: true)
        selectCurrentNote()
    }

    private func reloadNotes() {
        rebuildNotes()
        outline.reloadItem(notes, reloadChildren: true)
        selectCurrentNote()
    }

    private func reloadStudy() {
        rebuildStudy()
        outline.reloadItem(study, reloadChildren: true)
    }

    private func rebuildNotes() {
        notes.children = AppServices.shared.library.entries.map { SidebarNode(.note($0)) }
    }

    private func rebuildPages() {
        guard let note = windowController?.note else { return }
        let titles = PageOutline.titles(of: note)
        pages.children = titles.indices.map { SidebarNode(.page($0 + 1, titles[$0])) }
        thumbnails = thumbnails.filter { $0.key <= note.pages.count }
    }

    private func rebuildStudy() {
        let session = AppServices.shared.study
        study.children = session.imports.map { SidebarNode(.studyImport($0)) }
            + session.extracting.map { SidebarNode(.extracting($0)) }
            + [SidebarNode(session.imports.isEmpty ? .addMaterial : .generate)]
    }

    private func selectCurrentNote() {
        guard let url = windowController?.noteDocument?.fileURL?.canonicalFile,
              let node = notes.children.first(where: { if case .note(let entry) = $0.kind { return entry.url == url }; return false })
        else {
            outline.deselectAll(nil)
            return
        }
        let row = outline.row(forItem: node)
        if row >= 0 { outline.selectRowIndexes([row], byExtendingSelection: false) }
    }

    private func thumbnail(forPage number: Int) -> NSImage? {
        guard let note = windowController?.note, number <= note.pages.count else { return nil }
        let page = note.pages[number - 1]
        if let cached = thumbnails[number], cached.page == page { return cached.image }
        let image = PageThumbnail.image(for: page, size: Self.thumbnailSize)
        thumbnails[number] = (page, image)
        return image
    }

    // MARK: Actions

    @objc private func showPalette(_ sender: Any?) {
        CommandPalette.shared.show(over: view.window)
    }

    @objc private func rowClicked(_ sender: Any?) {
        guard let node = outline.item(atRow: outline.clickedRow) as? SidebarNode else { return }
        switch node.kind {
        case .note(let entry):
            if entry.url != windowController?.noteDocument?.fileURL?.canonicalFile {
                NoteDocuments.open(entry.url)
            }
        case .page(let number, _):
            windowController?.scrollToPage(number)
        case .studyImport, .extracting, .addMaterial:
            StudyPanelController.present(over: view.window)
        case .generate:
            StudyPanelController.present(over: view.window, generating: true)
        case .section:
            break
        }
        selectCurrentNote()
    }

    // MARK: NSOutlineViewDataSource

    func outlineView(_ outlineView: NSOutlineView, numberOfChildrenOfItem item: Any?) -> Int {
        (item as? SidebarNode)?.children.count ?? 3
    }

    func outlineView(_ outlineView: NSOutlineView, child index: Int, ofItem item: Any?) -> Any {
        (item as? SidebarNode)?.children[index] ?? [notes, pages, study][index]
    }

    func outlineView(_ outlineView: NSOutlineView, isItemExpandable item: Any) -> Bool {
        (item as? SidebarNode)?.isSection ?? false
    }

    // MARK: NSOutlineViewDelegate

    func outlineView(_ outlineView: NSOutlineView, isGroupItem item: Any) -> Bool {
        (item as? SidebarNode)?.isSection ?? false
    }

    func outlineView(_ outlineView: NSOutlineView, shouldSelectItem item: Any) -> Bool {
        if case .note = (item as? SidebarNode)?.kind { return true }
        return false
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
        }
        return cell
    }
}

/// An outline row. Sections own their rows.
final class SidebarNode {
    enum Kind {
        case section(String)
        case note(NoteEntry)
        case page(Int, String)
        case studyImport(StudyImport)
        case extracting(String)
        case addMaterial
        case generate
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
