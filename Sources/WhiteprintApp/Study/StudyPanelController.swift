import AppKit
import UniformTypeIdentifiers
import WhiteprintExtract
import WhiteprintStudy

/// The study sheet: drop course material in, watch extraction, and start
/// Claude Code to turn it into a study plan.
final class StudyPanelController: NSViewController, NSTableViewDataSource, NSTableViewDelegate {
    private static weak var visible: StudyPanelController?

    private var session: StudySession { AppServices.shared.study }
    private let table = NSTableView()
    private let errors = NSTextField(wrappingLabelWithString: "")
    private let status = NSTextView()
    private let statusScroll = NSScrollView()
    private let claudeHelp = NSTextField(wrappingLabelWithString: "")
    private let generateButton = NSButton(title: "Generate study plan", target: nil, action: nil)
    private let cancelButton = NSButton(title: "Cancel", target: nil, action: nil)
    private var observer: NSObjectProtocol?
    private var startWhenReady = false

    /// Shows the panel as a sheet on `window` (or as its own window), or
    /// brings an open one forward. With `generating`, starts a run right away
    /// when there's material and Claude Code is installed.
    static func present(over window: NSWindow?, generating: Bool = false) {
        if let open = visible, let panel = open.view.window {
            panel.makeKeyAndOrderFront(nil)
            if generating { open.generate(nil) }
            return
        }
        let controller = StudyPanelController()
        controller.startWhenReady = generating
        let panel = NSWindow(contentViewController: controller)
        panel.title = "Study Material"
        panel.styleMask = [.titled, .closable, .resizable]
        if let window, window.attachedSheet == nil {
            window.beginSheet(panel)
        } else {
            panel.center()
            panel.makeKeyAndOrderFront(nil)
        }
        visible = controller
    }

    deinit {
        observer.map(NotificationCenter.default.removeObserver)
    }

    override func loadView() {
        let title = NSTextField(labelWithString: "Study plan")
        title.font = .systemFont(ofSize: 17, weight: .semibold)
        let subtitle = NSTextField(wrappingLabelWithString: "Add lecture slides, PDFs or Word files. Claude reads them through Whiteprint and writes a study plan note with must-know topics, a learning path and a to-do list.")
        subtitle.textColor = .secondaryLabelColor

        let drop = DropZone { [weak self] urls in self?.session.importFiles(urls) }
        let importButton = NSButton(title: "Import…", target: self, action: #selector(chooseFiles(_:)))
        drop.addArrangedButton(importButton)

        let column = NSTableColumn(identifier: .init("import"))
        table.addTableColumn(column)
        table.headerView = nil
        table.rowHeight = 30
        table.style = .plain
        table.dataSource = self
        table.delegate = self
        table.backgroundColor = .clear
        table.selectionHighlightStyle = .none
        let tableScroll = NSScrollView()
        tableScroll.documentView = table
        tableScroll.hasVerticalScroller = true
        tableScroll.drawsBackground = false
        tableScroll.borderType = .noBorder

        errors.textColor = .systemRed
        errors.font = .systemFont(ofSize: 12)

        let privacy = NSTextField(wrappingLabelWithString: "Text is extracted on your Mac. Only extracted text is shared with your own Claude client.")
        privacy.textColor = .secondaryLabelColor
        privacy.font = .systemFont(ofSize: 11)
        let lock = NSImageView(image: NSImage(systemSymbolName: "lock", accessibilityDescription: nil)!)
        lock.contentTintColor = .secondaryLabelColor
        let privacyRow = NSStackView(views: [lock, privacy])
        privacyRow.alignment = .firstBaseline
        privacyRow.spacing = 6

        status.isEditable = false
        status.drawsBackground = false
        status.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        status.textColor = .secondaryLabelColor
        status.textContainerInset = NSSize(width: 4, height: 4)
        status.isVerticallyResizable = true
        status.autoresizingMask = .width
        statusScroll.documentView = status
        statusScroll.hasVerticalScroller = true
        statusScroll.borderType = .lineBorder
        statusScroll.drawsBackground = false

        claudeHelp.font = .systemFont(ofSize: 12)
        claudeHelp.isSelectable = true

        let close = NSButton(title: "Close", target: self, action: #selector(closePanel(_:)))
        close.keyEquivalent = "\u{1b}"
        cancelButton.target = self
        cancelButton.action = #selector(cancelRun(_:))
        generateButton.target = self
        generateButton.action = #selector(generate(_:))
        generateButton.keyEquivalent = "\r"
        generateButton.image = NSImage(systemSymbolName: "sparkles", accessibilityDescription: nil)
        generateButton.imagePosition = .imageLeading
        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let buttons = NSStackView(views: [close, spacer, cancelButton, generateButton])

        let stack = NSStackView(views: [title, subtitle, drop, tableScroll, errors, privacyRow, claudeHelp, statusScroll, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 20, bottom: 20, right: 20)
        stack.setCustomSpacing(4, after: title)
        for view in [subtitle, drop, tableScroll, errors, privacyRow, claudeHelp, statusScroll, buttons] {
            view.translatesAutoresizingMaskIntoConstraints = false
            view.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40).isActive = true
        }
        NSLayoutConstraint.activate([
            stack.widthAnchor.constraint(greaterThanOrEqualToConstant: 520),
            drop.heightAnchor.constraint(equalToConstant: 96),
            tableScroll.heightAnchor.constraint(equalToConstant: 120),
            statusScroll.heightAnchor.constraint(equalToConstant: 110),
        ])
        view = stack
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        observer = NotificationCenter.default.addObserver(forName: .studySessionDidChange, object: nil, queue: .main) { [weak self] _ in
            self?.update()
        }
        session.locateClaude()
        update()
    }

    private func update() {
        table.reloadData()
        errors.stringValue = session.importErrors.joined(separator: "\n")
        errors.isHidden = session.importErrors.isEmpty

        let claude: URL?? = session.claudeLookup
        switch claude {
        case .none:
            claudeHelp.stringValue = "Looking for Claude Code…"
            claudeHelp.isHidden = false
        case .some(.none):
            claudeHelp.stringValue = """
            Claude Code wasn't found. For the one-click button, install it from claude.com/claude-code, then run "claude" once in Terminal and log in with your Claude account.
            Or use Claude Desktop: connect Whiteprint in Settings (⌘,), then pick the "study_plan" prompt.
            """
            claudeHelp.isHidden = false
        case .some(.some):
            claudeHelp.isHidden = true
        }

        let lines = session.log
        status.string = lines.joined(separator: "\n")
        status.scrollToEndOfDocument(nil)
        statusScroll.isHidden = lines.isEmpty

        let hasClaude = { if case .some(.some) = claude { return true }; return false }()
        generateButton.isEnabled = hasClaude && !session.isRunning && !session.imports.isEmpty && session.extracting.isEmpty
        cancelButton.isHidden = !session.isRunning

        if startWhenReady, claude != nil {
            startWhenReady = false
            if generateButton.isEnabled { generate(nil) }
        }
    }

    // MARK: Actions

    @objc private func chooseFiles(_ sender: Any?) {
        let open = NSOpenPanel()
        open.allowsMultipleSelection = true
        open.canChooseDirectories = false
        open.allowedContentTypes = DocumentExtractor.supportedExtensions.compactMap { UTType(filenameExtension: $0) }
        open.message = "Choose PDF, Word or PowerPoint files"
        guard let window = view.window else { return }
        open.beginSheetModal(for: window) { [weak self] response in
            if response == .OK { self?.session.importFiles(open.urls) }
        }
    }

    @objc func generate(_ sender: Any?) {
        guard generateButton.isEnabled else { return }
        session.generate()
    }

    @objc private func cancelRun(_ sender: Any?) {
        session.cancel()
    }

    @objc private func closePanel(_ sender: Any?) {
        guard let panel = view.window else { return }
        if let parent = panel.sheetParent {
            parent.endSheet(panel)
        } else {
            panel.close()
        }
    }

    @objc private func removeImport(_ sender: NSButton) {
        let row = table.row(for: sender)
        guard row >= 0, row < session.imports.count else { return }
        session.remove(session.imports[row].id)
    }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int {
        max(1, session.imports.count + session.extracting.count)
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let imports = session.imports
        if imports.isEmpty && session.extracting.isEmpty {
            let empty = NSTextField(labelWithString: "No material yet.")
            empty.textColor = .tertiaryLabelColor
            return empty
        }
        let icon = NSImageView()
        let name = NSTextField(labelWithString: "")
        name.lineBreakMode = .byTruncatingMiddle
        name.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let detail = NSTextField(labelWithString: "")
        detail.textColor = .secondaryLabelColor
        detail.font = .monospacedDigitSystemFont(ofSize: 11, weight: .regular)
        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        var views: [NSView] = [icon, name, spacer]

        if row < imports.count {
            let item = imports[row]
            icon.image = NSImage(systemSymbolName: "doc.richtext", accessibilityDescription: nil)
            name.stringValue = item.name
            detail.stringValue = "\(BridgeService.summary(of: item, includingID: false)) · \(item.chunksDone)/\(item.chunkCount) read"
            let progress = NSProgressIndicator()
            progress.style = .bar
            progress.isIndeterminate = false
            progress.minValue = 0
            progress.maxValue = Double(max(1, item.chunkCount))
            progress.doubleValue = Double(item.chunksDone)
            progress.controlSize = .small
            progress.widthAnchor.constraint(equalToConstant: 70).isActive = true
            let remove = NSButton(image: NSImage(systemSymbolName: "xmark.circle.fill", accessibilityDescription: "Remove")!, target: self, action: #selector(removeImport(_:)))
            remove.isBordered = false
            remove.contentTintColor = .tertiaryLabelColor
            remove.toolTip = "Remove \(item.name)"
            remove.isEnabled = !session.isRunning
            views += [detail, progress, remove]
        } else {
            icon.image = NSImage(systemSymbolName: "hourglass", accessibilityDescription: nil)
            name.stringValue = session.extracting[row - imports.count]
            detail.stringValue = "Extracting text…"
            let spinner = NSProgressIndicator()
            spinner.style = .spinning
            spinner.controlSize = .small
            spinner.startAnimation(nil)
            views += [detail, spinner]
        }
        icon.contentTintColor = .secondaryLabelColor
        let stack = NSStackView(views: views)
        stack.spacing = 8
        return stack
    }
}

/// A dashed drop target for course files.
private final class DropZone: NSView {
    private let onDrop: ([URL]) -> Void
    private let stack = NSStackView()
    private var isTargeted = false {
        didSet { needsDisplay = true }
    }

    init(onDrop: @escaping ([URL]) -> Void) {
        self.onDrop = onDrop
        super.init(frame: .zero)
        registerForDraggedTypes([.fileURL])
        let icon = NSImageView(image: NSImage(systemSymbolName: "tray.and.arrow.down", accessibilityDescription: nil)!)
        icon.contentTintColor = .secondaryLabelColor
        icon.symbolConfiguration = .init(pointSize: 20, weight: .regular)
        let label = NSTextField(labelWithString: "Drop PDF, Word or PowerPoint files here")
        label.textColor = .secondaryLabelColor
        stack.setViews([icon, label], in: .center)
        stack.orientation = .vertical
        stack.spacing = 6
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.centerXAnchor.constraint(equalTo: centerXAnchor),
            stack.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    func addArrangedButton(_ button: NSButton) {
        button.controlSize = .small
        stack.addArrangedSubview(button)
    }

    override func draw(_ dirtyRect: NSRect) {
        let path = NSBezierPath(roundedRect: bounds.insetBy(dx: 1, dy: 1), xRadius: 8, yRadius: 8)
        path.lineWidth = 1.5
        path.setLineDash([5, 4], count: 2, phase: 0)
        (isTargeted ? NSColor.controlAccentColor : NSColor.separatorColor).setStroke()
        if isTargeted {
            NSColor.controlAccentColor.withAlphaComponent(0.08).setFill()
            path.fill()
        }
        path.stroke()
    }

    private func fileURLs(_ info: NSDraggingInfo) -> [URL] {
        let urls = info.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        return urls.filter { DocumentExtractor.supportedExtensions.contains($0.pathExtension.lowercased()) }
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        isTargeted = !fileURLs(sender).isEmpty
        return isTargeted ? .copy : []
    }

    override func draggingExited(_ sender: NSDraggingInfo?) {
        isTargeted = false
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        isTargeted = false
        let urls = fileURLs(sender)
        guard !urls.isEmpty else { return false }
        onDrop(urls)
        return true
    }
}
