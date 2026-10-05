import AppKit
import WhiteprintEditor

/// The note window's Touch Bar: app actions around the editor's own
/// formatting items, which appear in the middle while typing. Buttons send
/// their actions up the responder chain; the layout and syntax controls
/// change `ViewPreferences` and follow it.
final class NoteTouchBar: NSObject, NSTouchBarDelegate {
    static let customizationID = "io.github.l1203012.whiteprint.note-window"

    enum Item {
        static let newNote = NSTouchBarItem.Identifier("io.github.l1203012.whiteprint.new-note")
        static let palette = NSTouchBarItem.Identifier("io.github.l1203012.whiteprint.palette")
        static let sidebar = NSTouchBarItem.Identifier("io.github.l1203012.whiteprint.sidebar")
        static let layout = NSTouchBarItem.Identifier("io.github.l1203012.whiteprint.layout")
        static let syntax = NSTouchBarItem.Identifier("io.github.l1203012.whiteprint.syntax")
        static let study = NSTouchBarItem.Identifier("io.github.l1203012.whiteprint.study")
    }

    private let preferences: ViewPreferences
    private weak var layoutControl: NSSegmentedControl?
    private weak var syntaxButton: NSButton?
    private var observer: NSObjectProtocol?

    init(preferences: ViewPreferences = .shared) {
        self.preferences = preferences
        super.init()
        observer = NotificationCenter.default.addObserver(forName: .viewPreferencesDidChange, object: preferences, queue: nil) { [weak self] _ in
            self?.update()
        }
    }

    deinit {
        observer.map(NotificationCenter.default.removeObserver)
    }

    func makeTouchBar() -> NSTouchBar {
        let bar = NSTouchBar()
        bar.delegate = self
        bar.customizationIdentifier = Self.customizationID
        bar.defaultItemIdentifiers = [Item.sidebar, Item.palette, Item.newNote, .otherItemsProxy, .flexibleSpace, Item.layout, Item.syntax, Item.study]
        bar.customizationAllowedItemIdentifiers = [
            Item.sidebar, Item.palette, Item.newNote, Item.layout, Item.syntax, Item.study, .flexibleSpace, .fixedSpaceSmall,
        ]
        return bar
    }

    func touchBar(_ touchBar: NSTouchBar, makeItemForIdentifier identifier: NSTouchBarItem.Identifier) -> NSTouchBarItem? {
        switch identifier {
        case Item.newNote:
            return button(identifier, "New Note", "square.and.pencil", #selector(AppDelegate.newNote(_:)))
        case Item.palette:
            return button(identifier, "Command Palette", "magnifyingglass", #selector(AppDelegate.showCommandPalette(_:)))
        case Item.sidebar:
            return button(identifier, "Toggle Sidebar", "sidebar.left", #selector(NSSplitViewController.toggleSidebar(_:)))
        case Item.study:
            let item = button(identifier, "Study", "rectangle.on.rectangle.angled", #selector(NoteWindowController.studyFlashcards(_:)))
            item.title = "Study"
            return item
        case Item.layout:
            let control = NSSegmentedControl(labels: PageLayoutMode.allCases.map(Self.title), trackingMode: .selectOne,
                                             target: self, action: #selector(layoutChanged(_:)))
            layoutControl = control
            let item = NSCustomTouchBarItem(identifier: identifier)
            item.view = control
            item.customizationLabel = "Page Layout"
            update()
            return item
        case Item.syntax:
            let toggle = NSButton(title: "Markdown", target: self, action: #selector(syntaxToggled(_:)))
            toggle.setButtonType(.pushOnPushOff)
            syntaxButton = toggle
            let item = NSCustomTouchBarItem(identifier: identifier)
            item.view = toggle
            item.customizationLabel = "Show Markdown Syntax"
            update()
            return item
        default:
            return nil
        }
    }

    static func title(_ mode: PageLayoutMode) -> String {
        switch mode {
        case .slides: return "Slides"
        case .a4: return "A4"
        }
    }

    private func button(_ identifier: NSTouchBarItem.Identifier, _ label: String, _ symbol: String, _ action: Selector) -> NSButtonTouchBarItem {
        let image = NSImage(systemSymbolName: symbol, accessibilityDescription: label) ?? NSImage()
        let item = NSButtonTouchBarItem(identifier: identifier, image: image, target: nil, action: action)
        item.customizationLabel = label
        return item
    }

    private func update() {
        layoutControl?.selectedSegment = PageLayoutMode.allCases.firstIndex(of: preferences.layoutMode) ?? 0
        syntaxButton?.state = preferences.showsMarkdownSyntax ? .on : .off
        syntaxButton?.bezelColor = preferences.showsMarkdownSyntax ? .controlAccentColor : nil
    }

    @objc private func layoutChanged(_ sender: NSSegmentedControl) {
        guard PageLayoutMode.allCases.indices.contains(sender.selectedSegment) else { return }
        preferences.layoutMode = PageLayoutMode.allCases[sender.selectedSegment]
    }

    @objc private func syntaxToggled(_ sender: NSButton) {
        preferences.showsMarkdownSyntax = sender.state == .on
    }
}
