import AppKit

/// One row of the command palette.
struct PaletteItem {
    enum Kind {
        case note, page, command
    }

    var kind: Kind
    var title: String
    var detail: String
    var symbol: String
    var run: () -> Void
}

/// ⌘K: a centred floating panel to jump to notes and pages and run commands.
/// Arrow keys move, Return runs, Escape (or clicking elsewhere) closes.
final class CommandPalette: NSObject, NSTextFieldDelegate, NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate {
    static let shared = CommandPalette()

    private lazy var panel = makePanel()
    private let field = NSTextField()
    private let table = NSTableView()
    private var items: [PaletteItem] = []
    private var shown: [PaletteItem] = []

    func show(over window: NSWindow?) {
        items = Self.items(for: window?.windowController as? NoteWindowController)
        field.stringValue = ""
        filter()
        let size = panel.frame.size
        let area = window?.frame ?? NSScreen.main?.visibleFrame ?? .zero
        panel.setFrameOrigin(NSPoint(x: area.midX - size.width / 2, y: area.maxY - size.height - area.height * 0.18))
        panel.makeKeyAndOrderFront(nil)
        panel.makeFirstResponder(field)
    }

    private func close() {
        panel.orderOut(nil)
    }

    // MARK: Items

    private static func items(for controller: NoteWindowController?) -> [PaletteItem] {
        let app = NSApp.delegate as? AppDelegate
        var items: [PaletteItem] = []
        let current = controller?.noteDocument?.fileURL?.canonicalFile
        for entry in AppServices.shared.library.entries {
            items.append(PaletteItem(kind: .note, title: entry.title, detail: entry.url == current ? "Open" : "Note", symbol: "doc.text") {
                NoteDocuments.open(entry.url)
            })
        }
        if let controller {
            for (index, title) in PageOutline.titles(of: controller.note).enumerated() {
                items.append(PaletteItem(kind: .page, title: title, detail: "Page \(index + 1)", symbol: "doc.plaintext") { [weak controller] in
                    controller?.scrollToPage(index + 1)
                    controller?.focusEditor()
                })
            }
        }
        items += [
            PaletteItem(kind: .command, title: "New note", detail: "⌘N", symbol: "plus", run: { app?.newNote(nil) }),
            PaletteItem(kind: .command, title: "Import for study plan", detail: "Study", symbol: "tray.and.arrow.down", run: { app?.showStudyPanel(nil) }),
            PaletteItem(kind: .command, title: "Generate study plan", detail: "Study", symbol: "sparkles", run: { app?.generateStudyPlan(nil) }),
            PaletteItem(kind: .command, title: "Settings", detail: "⌘,", symbol: "gearshape", run: { app?.showSettings(nil) }),
        ]
        if let controller {
            let document = controller.noteDocument
            let send = { (action: Selector, target: AnyObject?) in
                { [weak target] in _ = NSApp.sendAction(action, to: target, from: nil) }
            }
            items += [
                PaletteItem(kind: .command, title: "Add page", detail: "⌥⌘N", symbol: "doc.badge.plus", run: send(#selector(NoteWindowController.addPage(_:)), controller)),
                PaletteItem(kind: .command, title: "Insert drawing", detail: "⇧⌘D", symbol: "pencil.and.outline", run: send(#selector(NoteWindowController.insertDrawing(_:)), controller)),
                PaletteItem(kind: .command, title: "Export as Markdown…", detail: "Export", symbol: "square.and.arrow.up", run: send(#selector(NoteDocument.exportMarkdown(_:)), document)),
                PaletteItem(kind: .command, title: "Export as PDF (Blueprint)…", detail: "Export", symbol: "square.and.arrow.up", run: send(#selector(NoteDocument.exportBlueprintPDF(_:)), document)),
                PaletteItem(kind: .command, title: "Export as PDF (Print)…", detail: "Export", symbol: "square.and.arrow.up", run: send(#selector(NoteDocument.exportPrintPDF(_:)), document)),
                PaletteItem(kind: .command, title: "Show in Finder", detail: "⌥⌘R", symbol: "folder", run: send(#selector(NoteDocument.showInFinder(_:)), document)),
            ]
        }
        return items
    }

    private func filter() {
        shown = PaletteSearch.filter(items, query: field.stringValue, title: \.title)
        table.reloadData()
        if !shown.isEmpty {
            table.selectRowIndexes([0], byExtendingSelection: false)
            table.scrollRowToVisible(0)
        }
    }

    private func runSelected() {
        let row = table.selectedRow
        guard row >= 0, row < shown.count else { return }
        let item = shown[row]
        close()
        // After the panel is gone, so sheets and focus go to the note window.
        DispatchQueue.main.async { item.run() }
    }

    private func moveSelection(by delta: Int) {
        guard !shown.isEmpty else { return }
        let row = min(max(table.selectedRow + delta, 0), shown.count - 1)
        table.selectRowIndexes([row], byExtendingSelection: false)
        table.scrollRowToVisible(row)
    }

    // MARK: NSTextFieldDelegate

    func controlTextDidChange(_ notification: Notification) {
        filter()
    }

    func control(_ control: NSControl, textView: NSTextView, doCommandBy selector: Selector) -> Bool {
        switch selector {
        case #selector(NSResponder.moveUp(_:)): moveSelection(by: -1)
        case #selector(NSResponder.moveDown(_:)): moveSelection(by: 1)
        case #selector(NSResponder.insertNewline(_:)): runSelected()
        case #selector(NSResponder.cancelOperation(_:)): close()
        default: return false
        }
        return true
    }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int {
        shown.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let item = shown[row]
        let icon = NSImageView(image: NSImage(systemSymbolName: item.symbol, accessibilityDescription: nil) ?? NSImage())
        icon.contentTintColor = .secondaryLabelColor
        icon.widthAnchor.constraint(equalToConstant: 18).isActive = true
        let title = NSTextField(labelWithString: item.title)
        title.font = .systemFont(ofSize: 14)
        title.lineBreakMode = .byTruncatingTail
        title.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let detail = NSTextField(labelWithString: item.detail)
        detail.font = .systemFont(ofSize: 12)
        detail.textColor = .tertiaryLabelColor
        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let stack = NSStackView(views: [icon, title, spacer, detail])
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 0, left: 10, bottom: 0, right: 12)
        return stack
    }

    @objc private func rowClicked(_ sender: Any?) {
        runSelected()
    }

    // MARK: Panel

    func windowDidResignKey(_ notification: Notification) {
        close()
    }

    private func makePanel() -> NSPanel {
        let panel = PalettePanel(
            contentRect: NSRect(x: 0, y: 0, width: 560, height: 380),
            styleMask: [.titled, .fullSizeContentView, .nonactivatingPanel],
            backing: .buffered, defer: true
        )
        panel.titleVisibility = .hidden
        panel.titlebarAppearsTransparent = true
        panel.isMovable = false
        panel.level = .floating
        panel.hidesOnDeactivate = true
        panel.isReleasedWhenClosed = false
        panel.delegate = self
        [.closeButton, .miniaturizeButton, .zoomButton].forEach { panel.standardWindowButton($0)?.isHidden = true }

        let background = NSVisualEffectView()
        background.material = .popover
        background.state = .active
        background.blendingMode = .behindWindow

        field.placeholderString = "Search notes, pages and commands"
        field.font = .systemFont(ofSize: 17)
        field.focusRingType = .none
        field.isBordered = false
        field.drawsBackground = false
        field.delegate = self
        let glass = NSImageView(image: NSImage(systemSymbolName: "magnifyingglass", accessibilityDescription: nil)!)
        glass.contentTintColor = .secondaryLabelColor
        glass.symbolConfiguration = .init(pointSize: 16, weight: .regular)

        let column = NSTableColumn(identifier: .init("item"))
        table.addTableColumn(column)
        table.headerView = nil
        table.rowHeight = 32
        table.style = .plain
        table.backgroundColor = .clear
        table.intercellSpacing = NSSize(width: 0, height: 2)
        table.dataSource = self
        table.delegate = self
        table.target = self
        table.action = #selector(rowClicked(_:))
        table.refusesFirstResponder = true

        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        let divider = NSBox()
        divider.boxType = .separator

        for view in [glass, field, divider, scroll] as [NSView] {
            view.translatesAutoresizingMaskIntoConstraints = false
            background.addSubview(view)
        }
        NSLayoutConstraint.activate([
            field.topAnchor.constraint(equalTo: background.topAnchor, constant: 14),
            glass.leadingAnchor.constraint(equalTo: background.leadingAnchor, constant: 16),
            glass.centerYAnchor.constraint(equalTo: field.centerYAnchor),
            field.leadingAnchor.constraint(equalTo: glass.trailingAnchor, constant: 8),
            field.trailingAnchor.constraint(equalTo: background.trailingAnchor, constant: -14),
            divider.topAnchor.constraint(equalTo: field.bottomAnchor, constant: 10),
            divider.leadingAnchor.constraint(equalTo: background.leadingAnchor),
            divider.trailingAnchor.constraint(equalTo: background.trailingAnchor),
            scroll.topAnchor.constraint(equalTo: divider.bottomAnchor, constant: 6),
            scroll.leadingAnchor.constraint(equalTo: background.leadingAnchor, constant: 6),
            scroll.trailingAnchor.constraint(equalTo: background.trailingAnchor, constant: -6),
            scroll.bottomAnchor.constraint(equalTo: background.bottomAnchor, constant: -6),
        ])
        panel.contentView = background
        return panel
    }
}

/// Borderless-looking panel that can still take keyboard focus.
private final class PalettePanel: NSPanel {
    override var canBecomeKey: Bool { true }
}
