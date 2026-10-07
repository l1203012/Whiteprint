import AppKit

/// The editor's Touch Bar while typing: three compact groups of formatting
/// buttons (Bold, Italic, Code · H1, H2, H3 · Bulleted list, Numbered list,
/// Checklist) and a More popover with the rest (Body, Quote, Code block,
/// Divider and the inserts). Every button runs an `EditorCommand`.
///
/// The groups are high priority, so when the window's own items share the
/// bar they give way first. Users can rearrange it with View ▸ Customize
/// Touch Bar, where every command is also offered as a button of its own.
final class EditorTouchBar: NSObject, NSTouchBarDelegate {
    static let customizationIdentifier = "io.github.l1203012.whiteprint.editor"
    static let inline = identifier("group.inline")
    static let headings = identifier("group.headings")
    static let lists = identifier("group.lists")
    static let more = identifier("more")

    /// The groups on the bar by default, each a row of commands.
    static let groups: [(identifier: NSTouchBarItem.Identifier, label: String, commands: [EditorCommand])] = [
        (inline, "Text Formatting", [.bold, .italic, .inlineCode]),
        (headings, "Headings", [.heading1, .heading2, .heading3]),
        (lists, "Lists", [.bulletList, .numberedList, .checklist]),
    ]
    /// The commands in the More popover.
    static let moreCommands: [EditorCommand] = [.body, .quote, .codeBlock, .divider, .drawing, .flashcards, .newPage]

    static func identifier(_ name: String) -> NSTouchBarItem.Identifier {
        NSTouchBarItem.Identifier("\(customizationIdentifier).\(name)")
    }

    static func identifier(for command: EditorCommand) -> NSTouchBarItem.Identifier {
        identifier(command.rawValue)
    }

    static var defaultItemIdentifiers: [NSTouchBarItem.Identifier] {
        groups.map(\.identifier) + [more]
    }

    /// The default items, plus a button for every command and spaces.
    static var customizationAllowedItemIdentifiers: [NSTouchBarItem.Identifier] {
        defaultItemIdentifiers + EditorCommand.allCases.map(identifier(for:)) + [.flexibleSpace, .fixedSpaceSmall]
    }

    /// A short label for buttons and segments: text for headings and body,
    /// nil where the command's symbol is used.
    static func label(_ command: EditorCommand) -> String? {
        switch command {
        case .heading1: return "H1"
        case .heading2: return "H2"
        case .heading3: return "H3"
        case .body: return "Body"
        default: return nil
        }
    }

    private static func image(_ command: EditorCommand) -> NSImage? {
        NSImage(systemSymbolName: command.symbolName, accessibilityDescription: command.title)
    }

    private let onCommand: (EditorCommand) -> Void
    private weak var morePopover: NSPopoverTouchBarItem?

    init(onCommand: @escaping (EditorCommand) -> Void) {
        self.onCommand = onCommand
    }

    func makeTouchBar() -> NSTouchBar {
        let bar = NSTouchBar()
        bar.delegate = self
        bar.customizationIdentifier = Self.customizationIdentifier
        bar.defaultItemIdentifiers = Self.defaultItemIdentifiers
        bar.customizationAllowedItemIdentifiers = Self.customizationAllowedItemIdentifiers
        return bar
    }

    func touchBar(_ touchBar: NSTouchBar, makeItemForIdentifier identifier: NSTouchBarItem.Identifier) -> NSTouchBarItem? {
        if identifier == Self.more {
            return makeMorePopover()
        }
        if let group = Self.groups.first(where: { $0.identifier == identifier }) {
            return makeGroup(identifier, label: group.label, commands: group.commands)
        }
        guard let command = EditorCommand.allCases.first(where: { Self.identifier(for: $0) == identifier }) else { return nil }
        return makeButton(command, identifier: identifier)
    }

    /// A momentary segmented control, one segment per command.
    private func makeGroup(_ identifier: NSTouchBarItem.Identifier, label: String, commands: [EditorCommand]) -> NSTouchBarItem {
        let control = NSSegmentedControl()
        control.trackingMode = .momentary
        control.segmentCount = commands.count
        for (index, command) in commands.enumerated() {
            if let text = Self.label(command) {
                control.setLabel(text, forSegment: index)
            } else {
                control.setImage(Self.image(command), forSegment: index)
            }
            control.setWidth(44, forSegment: index)
            control.setToolTip(command.title, forSegment: index)
        }
        control.identifier = NSUserInterfaceItemIdentifier(identifier.rawValue)
        control.target = self
        control.action = #selector(runSegment(_:))
        control.setAccessibilityLabel(label)
        let item = NSCustomTouchBarItem(identifier: identifier)
        item.view = control
        item.customizationLabel = label
        item.visibilityPriority = .high
        return item
    }

    private func makeMorePopover() -> NSTouchBarItem {
        let item = NSPopoverTouchBarItem(identifier: Self.more)
        item.customizationLabel = "More Formatting"
        item.collapsedRepresentationImage = NSImage(systemSymbolName: "ellipsis.circle", accessibilityDescription: "More Formatting")
        let bar = NSTouchBar()
        bar.delegate = self
        bar.defaultItemIdentifiers = Self.moreCommands.map(Self.identifier(for:))
        item.popoverTouchBar = bar
        item.pressAndHoldTouchBar = bar
        morePopover = item
        return item
    }

    private func makeButton(_ command: EditorCommand, identifier: NSTouchBarItem.Identifier) -> NSTouchBarItem {
        let item: NSButtonTouchBarItem
        if let text = Self.label(command) {
            item = NSButtonTouchBarItem(identifier: identifier, title: text, target: self, action: #selector(run(_:)))
        } else {
            item = NSButtonTouchBarItem(identifier: identifier, image: Self.image(command) ?? NSImage(),
                                        target: self, action: #selector(run(_:)))
        }
        item.customizationLabel = command.title
        return item
    }

    @objc private func runSegment(_ sender: NSSegmentedControl) {
        guard let group = Self.groups.first(where: { $0.identifier.rawValue == sender.identifier?.rawValue }),
              group.commands.indices.contains(sender.selectedSegment) else { return }
        onCommand(group.commands[sender.selectedSegment])
    }

    @objc private func run(_ sender: NSButtonTouchBarItem) {
        guard let command = EditorCommand.allCases.first(where: { Self.identifier(for: $0) == sender.identifier }) else { return }
        if Self.moreCommands.contains(command) { morePopover?.dismissPopover(nil) }
        onCommand(command)
    }
}
