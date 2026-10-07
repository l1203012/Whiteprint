import AppKit
import WhiteprintBridge
import WhiteprintRender
import WhiteprintStudy

/// Settings: the AI provider and connecting Claude, the page theme, the notes
/// folder, and the study cache.
final class SettingsWindowController: NSWindowController {
    init() {
        let tabs = NSTabViewController()
        tabs.tabStyle = .toolbar
        tabs.addTabViewItem(Self.tab(AISettingsViewController(), "AI", "sparkles"))
        tabs.addTabViewItem(Self.tab(AppearanceSettingsViewController(), "Appearance", "paintpalette"))
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

// MARK: - AI

private final class AISettingsViewController: SettingsPane {
    private let settings = AISettings.shared
    private let providerPopup = NSPopUpButton()
    private let apiKeyField = NSSecureTextField()
    private let modelField = NSTextField()
    private var keyStatus: NSTextField!
    private var testStatus: NSTextField!
    private var testButton: NSButton!
    private var codeStatus: NSTextField!
    private var desktopStatus: NSTextField!
    private var command: NSTextField!
    private var addButton: NSButton!
    private var claude: URL?
    private let helper = BridgePaths.helperExecutable

    override func build() {
        buildProvider()
        buildGrok()

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

    private func buildProvider() {
        header("Study plans")
        note("“Generate study plan” reads your imported material with:")
        providerPopup.addItems(withTitles: AIProvider.allCases.map(\.title))
        providerPopup.selectItem(at: AIProvider.allCases.firstIndex(of: settings.provider) ?? 0)
        providerPopup.target = self
        providerPopup.action = #selector(providerChanged(_:))
        row(providerPopup)
    }

    private func buildGrok() {
        header("Grok")
        note("Uses xAI’s API with your own key, billed by xAI. The key is kept in your Keychain, never in preferences or logs.")
        apiKeyField.placeholderString = "xai-…"
        apiKeyField.widthAnchor.constraint(equalToConstant: 260).isActive = true
        row(label("API key"), apiKeyField,
            NSButton(title: "Save", target: self, action: #selector(saveKey(_:))),
            NSButton(title: "Remove", target: self, action: #selector(removeKey(_:))))
        keyStatus = note("")
        modelField.stringValue = settings.grokModel
        modelField.placeholderString = GrokRunner.Configuration.defaultModel
        modelField.widthAnchor.constraint(equalToConstant: 260).isActive = true
        modelField.target = self
        modelField.action = #selector(modelChanged(_:))
        testButton = NSButton(title: "Test Connection", target: self, action: #selector(testConnection(_:)))
        row(label("Model"), modelField, testButton)
        testStatus = note("")
        updateKeyStatus()
    }

    private func label(_ text: String) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.alignment = .right
        label.widthAnchor.constraint(equalToConstant: 56).isActive = true
        return label
    }

    private func updateKeyStatus() {
        let saved = settings.apiKeyItem.read() != nil
        keyStatus.stringValue = saved ? "✓ A key is saved in your Keychain." : "No key saved."
        apiKeyField.placeholderString = saved ? "Saved — type a new key to replace it" : "xai-…"
        testButton.isEnabled = saved
    }

    @objc private func providerChanged(_ sender: NSPopUpButton) {
        settings.provider = AIProvider.allCases[max(0, sender.indexOfSelectedItem)]
        NotificationCenter.default.post(name: .studySessionDidChange, object: nil)
    }

    @objc private func saveKey(_ sender: Any?) {
        let key = apiKeyField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !key.isEmpty else { return }
        do {
            try settings.apiKeyItem.save(key)
            apiKeyField.stringValue = ""
            testStatus.stringValue = ""
        } catch {
            inform("Couldn't save the key in the Keychain.", errorLine(error), style: .warning)
        }
        updateKeyStatus()
        NotificationCenter.default.post(name: .studySessionDidChange, object: nil)
    }

    @objc private func removeKey(_ sender: Any?) {
        do {
            try settings.apiKeyItem.delete()
        } catch {
            inform("Couldn't remove the key from the Keychain.", errorLine(error), style: .warning)
        }
        updateKeyStatus()
        NotificationCenter.default.post(name: .studySessionDidChange, object: nil)
    }

    @objc private func modelChanged(_ sender: Any?) {
        settings.grokModel = modelField.stringValue
        modelField.stringValue = settings.grokModel
    }

    @objc private func testConnection(_ sender: Any?) {
        modelChanged(nil)
        if !apiKeyField.stringValue.trimmingCharacters(in: .whitespaces).isEmpty { saveKey(nil) }
        guard let configuration = settings.grokConfiguration else { return }
        testButton.isEnabled = false
        testStatus.stringValue = "Testing \(configuration.model)…"
        GrokRunner.testConnection(configuration) { [weak self] result in
            guard let self else { return }
            self.testButton.isEnabled = true
            switch result {
            case .success(let message):
                self.testStatus.stringValue = "✓ " + (message.isEmpty ? "Connected." : message)
            case .failure(let error):
                self.testStatus.stringValue = "✗ " + errorLine(error)
            }
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

// MARK: - Appearance

private final class AppearanceSettingsViewController: SettingsPane {
    private let themePopup = NSPopUpButton()
    private var observer: NSObjectProtocol?

    deinit {
        observer.map(NotificationCenter.default.removeObserver)
    }

    override func build() {
        header("Page theme")
        note("How pages look while you write. PDF export has its own Blueprint and Print styles.")
        themePopup.addItems(withTitles: PageTheme.allCases.map(\.title))
        themePopup.target = self
        themePopup.action = #selector(themeChanged(_:))
        row(themePopup)
        update()
        observer = NotificationCenter.default.addObserver(forName: .viewPreferencesDidChange, object: nil, queue: .main) { [weak self] _ in
            self?.update()
        }
    }

    private func update() {
        themePopup.selectItem(at: PageTheme.allCases.firstIndex(of: ViewPreferences.shared.pageTheme) ?? 0)
    }

    @objc private func themeChanged(_ sender: NSPopUpButton) {
        ViewPreferences.shared.pageTheme = PageTheme.allCases[max(0, sender.indexOfSelectedItem)]
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
