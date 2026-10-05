import AppKit

/// The editor's Touch Bar while typing: a text style popover (H1/H2/H3/Body),
/// Bold, Italic, Code, Checklist, Bulleted list, Insert drawing, Insert
/// flashcards and New page. Every button runs an `EditorCommand`.
/// Users can rearrange it with View ▸ Customize Touch Bar.
final class EditorTouchBar: NSObject, NSTouchBarDelegate {
    static let customizationIdentifier = "io.github.l1203012.whiteprint.editor"
    static let style = identifier("style")

    /// The commands with a button of their own, in default order.
    static let buttonCommands: [EditorCommand] = [
        .bold, .italic, .inlineCode, .checklist, .bulletList, .drawing, .flashcards, .newPage,
    ]
    /// The commands in the style popover.
    static let styleCommands: [EditorCommand] = [.heading1, .heading2, .heading3, .body]

    static func identifier(_ name: String) -> NSTouchBarItem.Identifier {
        NSTouchBarItem.Identifier("\(customizationIdentifier).\(name)")
    }

    static func identifier(for command: EditorCommand) -> NSTouchBarItem.Identifier {
        identifier(command.rawValue)
    }

    static var defaultItemIdentifiers: [NSTouchBarItem.Identifier] {
        [style] + buttonCommands.map(identifier(for:))
    }

    private let onCommand: (EditorCommand) -> Void
    private weak var stylePopover: NSPopoverTouchBarItem?

    init(onCommand: @escaping (EditorCommand) -> Void) {
        self.onCommand = onCommand
    }

    func makeTouchBar() -> NSTouchBar {
        let bar = NSTouchBar()
        bar.delegate = self
        bar.customizationIdentifier = Self.customizationIdentifier
        bar.defaultItemIdentifiers = Self.defaultItemIdentifiers
        bar.customizationAllowedItemIdentifiers = Self.defaultItemIdentifiers + [.flexibleSpace, .fixedSpaceSmall]
        return bar
    }

    func touchBar(_ touchBar: NSTouchBar, makeItemForIdentifier identifier: NSTouchBarItem.Identifier) -> NSTouchBarItem? {
        if identifier == Self.style {
            return makeStylePopover()
        }
        let commands = Self.buttonCommands + Self.styleCommands
        guard let command = commands.first(where: { Self.identifier(for: $0) == identifier }) else { return nil }
        return makeButton(command, identifier: identifier)
    }

    private func makeStylePopover() -> NSTouchBarItem {
        let item = NSPopoverTouchBarItem(identifier: Self.style)
        item.customizationLabel = "Text Style"
        item.collapsedRepresentationImage = NSImage(systemSymbolName: "textformat.size", accessibilityDescription: "Text Style")
        let bar = NSTouchBar()
        bar.delegate = self
        bar.defaultItemIdentifiers = Self.styleCommands.map(Self.identifier(for:))
        item.popoverTouchBar = bar
        item.pressAndHoldTouchBar = bar
        stylePopover = item
        return item
    }

    private func makeButton(_ command: EditorCommand, identifier: NSTouchBarItem.Identifier) -> NSTouchBarItem {
        let item = NSButtonTouchBarItem(identifier: identifier, title: "", target: self, action: #selector(run(_:)))
        switch command {
        case .heading1: item.title = "H1"
        case .heading2: item.title = "H2"
        case .heading3: item.title = "H3"
        case .body: item.title = "Body"
        default: item.image = NSImage(systemSymbolName: command.symbolName, accessibilityDescription: command.title)
        }
        item.customizationLabel = command.title
        return item
    }

    @objc private func run(_ sender: NSButtonTouchBarItem) {
        guard let command = EditorCommand.allCases.first(where: { Self.identifier(for: $0) == sender.identifier }) else { return }
        if Self.styleCommands.contains(command) { stylePopover?.dismissPopover(nil) }
        onCommand(command)
    }
}
