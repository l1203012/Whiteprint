import AppKit
import WhiteprintBridge
import WhiteprintStudy

/// Settings: connecting Claude, the notes folder, and the study cache.
final class SettingsWindowController: NSWindowController {
    init() {
        let tabs = NSTabViewController()
        tabs.tabStyle = .toolbar
        tabs.addTabViewItem(Self.tab(ClaudeSettingsViewController(), "Claude", "sparkles"))
        tabs.addTabViewItem(Self.tab(NotesSettingsViewController(), "Notes", "folder"))
        tabs.addTabViewItem(Self.tab(StudySettingsViewController(), "Study", "graduationcap"))
        let window = NSWindow(contentViewController: tabs)
        window.styleMask = [.titled, .closable]
        window.title = tabs.tabViewItems[0].label
        window.isReleasedWhenClosed = false
        super.init(window: window)
        window.center()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    private static func tab(_ controller: NSViewController, _ label: String, _ symbol: String) -> NSTabViewItem {
        let item = NSTabViewItem(viewController: controller)
        item.label = label
        item.image = NSImage(systemSymbolName: symbol, accessibilityDescription: label)
        return item
    }
}

/// A vertical form with headers, notes and button rows.
private class SettingsPane: NSViewController {
    let stack = NSStackView()

    override func loadView() {
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 24, bottom: 24, right: 24)
        stack.widthAnchor.constraint(equalToConstant: 540).isActive = true
        view = stack
        build()
    }

    func build() {}

    func header(_ text: String) {
        if !stack.arrangedSubviews.isEmpty {
            stack.setCustomSpacing(22, after: stack.arrangedSubviews.last!)
        }
        let label = NSTextField(labelWithString: text)
        label.font = .systemFont(ofSize: 13, weight: .semibold)
        stack.addArrangedSubview(label)
    }

    @discardableResult
    func note(_ text: String, secondary: Bool = true) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.textColor = secondary ? .secondaryLabelColor : .labelColor
        label.font = .systemFont(ofSize: 12)
        label.preferredMaxLayoutWidth = 492
        stack.addArrangedSubview(label)
        return label
    }

    func row(_ views: NSView...) {
        let row = NSStackView(views: views)
        row.spacing = 8
        stack.addArrangedSubview(row)
    }

    /// A selectable monospaced command with a Copy button.
    func copyable(_ text: String) -> NSTextField {
        let field = NSTextField(wrappingLabelWithString: text)
        field.isSelectable = true
        field.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        field.preferredMaxLayoutWidth = 420
        field.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let copy = CopyButton(field: field)
        let row = NSStackView(views: [field, copy])
        row.alignment = .top
        row.spacing = 8
        stack.addArrangedSubview(row)
        return field
    }

    func confirm(_ message: String, _ detail: String, button: String) -> Bool {
        let alert = NSAlert()
        alert.messageText = message
        alert.informativeText = detail
        alert.addButton(withTitle: button)
        alert.addButton(withTitle: "Cancel")
        return alert.runModal() == .alertFirstButtonReturn
    }

    func inform(_ message: String, _ detail: String, style: NSAlert.Style = .informational) {
        let alert = NSAlert()
        alert.alertStyle = style
        alert.messageText = message
        alert.informativeText = detail
        if let window = view.window {
            alert.beginSheetModal(for: window)
        } else {
            alert.runModal()
        }
    }
}

private final class CopyButton: NSButton {
    private weak var field: NSTextField?

    convenience init(field: NSTextField) {
        self.init(title: "Copy", target: nil, action: nil)
        self.field = field
        target = self
        action = #selector(copyText(_:))
        controlSize = .small
    }

    @objc private func copyText(_ sender: Any?) {
        guard let text = field?.stringValue else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}

// MARK: - Claude

private final class ClaudeSettingsViewController: SettingsPane {
    private var codeStatus: NSTextField!
    private var desktopStatus: NSTextField!
    private var command: NSTextField!
    private var addButton: NSButton!
    private var claude: URL?
    private let helper = BridgePaths.helperExecutable

    override func build() {
        header("Claude Code")
        codeStatus = note("Looking for Claude Code…", secondary: false)
        note("Adds Whiteprint as an MCP server for your user, so every Claude Code session can read and write your notes. One-click study plans use your logged-in Claude Code.")
        addButton = NSButton(title: "Add Whiteprint to Claude Code", target: self, action: #selector(addToClaudeCode(_:)))
        addButton.isEnabled = false
        row(addButton)
        note("Or run this in Terminal:")
        command = copyable(ClaudeCodeSetup.commandLine(claude: nil, helper: helper))

        header("Claude Desktop")
        desktopStatus = note("", secondary: false)
        note("Adds Whiteprint to Claude Desktop's MCP servers. Your other servers are kept, and the old file is saved as claude_desktop_config.json.backup. Restart Claude Desktop afterwards, then pick the “study_plan” prompt to build a study plan.")
        row(NSButton(title: "Connect Claude Desktop", target: self, action: #selector(connectDesktop(_:))))
        note("Or add this to claude_desktop_config.json yourself:")
        _ = copyable(ClaudeDesktopConfig.snippet(helper: helper))

        updateDesktopStatus()
        DispatchQueue.global(qos: .userInitiated).async {
            let url = ClaudeCodeRunner.locateClaude()
            DispatchQueue.main.async { self.claudeFound(url) }
        }
    }

    private func claudeFound(_ url: URL?) {
        claude = url
        addButton.isEnabled = url != nil
        command.stringValue = ClaudeCodeSetup.commandLine(claude: url, helper: helper)
        codeStatus.stringValue = url.map { "✓ Found at \(($0.path as NSString).abbreviatingWithTildeInPath)" }
            ?? "Not found. Install Claude Code from claude.com/claude-code, then run “claude” once in Terminal to log in."
    }

    private func updateDesktopStatus() {
        desktopStatus.stringValue = ClaudeDesktopConfig.isConnected(helper: helper)
            ? "✓ Connected" : "Not connected"
    }

    @objc private func addToClaudeCode(_ sender: Any?) {
        guard let claude else { return }
        guard confirm(
            "Add Whiteprint to Claude Code?",
            "This runs:\n\(ClaudeCodeSetup.commandLine(claude: claude, helper: helper))",
            button: "Add"
        ) else { return }
        addButton.isEnabled = false
        ClaudeCodeSetup.register(claude: claude, helper: helper) { [weak self] result in
            guard let self else { return }
            self.addButton.isEnabled = true
            switch result {
            case .success:
                self.inform("Whiteprint was added to Claude Code.", "Start a new Claude Code session to use it.")
            case .failure(let error):
                self.inform("Couldn't add Whiteprint to Claude Code.", error.localizedDescription, style: .warning)
            }
        }
    }

    @objc private func connectDesktop(_ sender: Any?) {
        let config = ClaudeDesktopConfig.defaultURL
        guard confirm(
            "Connect Claude Desktop?",
            "Whiteprint will add itself to \((config.path as NSString).abbreviatingWithTildeInPath) and keep a backup of the current file.",
            button: "Connect"
        ) else { return }
        do {
            try ClaudeDesktopConfig.connect(helper: helper, configURL: config)
            inform("Claude Desktop is connected.", "Quit and reopen Claude Desktop to load Whiteprint.")
        } catch {
            inform("Couldn't update the Claude Desktop config.", errorLine(error), style: .warning)
        }
        updateDesktopStatus()
    }
}

// MARK: - Notes

private final class NotesSettingsViewController: SettingsPane {
    private var path: NSTextField!

    override func build() {
        header("Notes folder")
        note("New notes are saved here, and the sidebar lists the notes in it.")
        path = note("", secondary: false)
        path.isSelectable = true
        row(
            NSButton(title: "Choose…", target: self, action: #selector(choose(_:))),
            NSButton(title: "Show in Finder", target: self, action: #selector(reveal(_:)))
        )
        update()
    }

    private func update() {
        path.stringValue = (AppServices.shared.library.folder.url.path as NSString).abbreviatingWithTildeInPath
    }

    @objc private func choose(_ sender: Any?) {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.directoryURL = AppServices.shared.library.folder.url
        panel.prompt = "Use Folder"
        guard let window = view.window else { return }
        panel.beginSheetModal(for: window) { [weak self] response in
            guard response == .OK, let url = panel.url else { return }
            AppServices.shared.library.changeFolder(to: url)
            self?.update()
        }
    }

    @objc private func reveal(_ sender: Any?) {
        NSWorkspace.shared.open(AppServices.shared.library.folder.url)
    }
}

// MARK: - Study

private final class StudySettingsViewController: SettingsPane {
    override func build() {
        header("Extracted text")
        note("Text extracted from imported files is cached on this Mac with the points Claude saved, so study plans can resume. Clearing it removes every import; your notes are not affected.")
        row(NSButton(title: "Clear Extracted Text Cache…", target: self, action: #selector(clear(_:))))
    }

    @objc private func clear(_ sender: Any?) {
        guard confirm("Clear the extracted text cache?", "All imports and saved points are removed. Your notes are kept.", button: "Clear") else { return }
        do {
            try AppServices.shared.study.removeAll()
            inform("The cache was cleared.", "")
        } catch {
            inform("Couldn't clear the cache.", errorLine(error), style: .warning)
        }
    }
}
