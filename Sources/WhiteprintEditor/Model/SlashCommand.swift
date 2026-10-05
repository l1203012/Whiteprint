import Foundation

/// The commands offered by the `/` menu.
enum SlashCommand: CaseIterable, Equatable {
    case heading1, heading2, heading3
    case bulletList, numberedList, checklist
    case quote, codeBlock, divider
    case drawing, newPage

    var title: String {
        switch self {
        case .heading1: return "Heading 1"
        case .heading2: return "Heading 2"
        case .heading3: return "Heading 3"
        case .bulletList: return "Bulleted list"
        case .numberedList: return "Numbered list"
        case .checklist: return "Checklist"
        case .quote: return "Quote"
        case .codeBlock: return "Code block"
        case .divider: return "Divider"
        case .drawing: return "Drawing"
        case .newPage: return "New page"
        }
    }

    var subtitle: String {
        switch self {
        case .heading1: return "Big section heading"
        case .heading2: return "Medium section heading"
        case .heading3: return "Small section heading"
        case .bulletList: return "A simple bulleted list"
        case .numberedList: return "A list with numbering"
        case .checklist: return "Track tasks with checkboxes"
        case .quote: return "Capture a quote"
        case .codeBlock: return "Monospaced code"
        case .divider: return "Visually divide blocks"
        case .drawing: return "A diagram in the drawing language"
        case .newPage: return "Start a new page after this one"
        }
    }

    /// Extra words the filter matches, besides the title.
    private var keywords: [String] {
        switch self {
        case .heading1: return ["h1", "title", "#"]
        case .heading2: return ["h2", "subtitle", "##"]
        case .heading3: return ["h3", "###"]
        case .bulletList: return ["ul", "unordered", "-"]
        case .numberedList: return ["ol", "ordered", "1."]
        case .checklist: return ["todo", "task", "checkbox", "[]"]
        case .quote: return ["blockquote", ">"]
        case .codeBlock: return ["```", "snippet", "pre"]
        case .divider: return ["hr", "rule", "line", "---", "separator"]
        case .drawing: return ["diagram", "sketch", "wp", "draw", "chart"]
        case .newPage: return ["page", "break", "+++"]
        }
    }

    /// Commands matching what was typed after the slash: word-prefix matches
    /// on the title first, then keyword prefixes, then substrings.
    static func matching(_ query: String) -> [SlashCommand] {
        let query = query.lowercased().trimmingCharacters(in: .whitespaces)
        guard !query.isEmpty else { return allCases }
        let squashed = query.replacingOccurrences(of: " ", with: "")
        func rank(_ command: SlashCommand) -> Int? {
            let title = command.title.lowercased()
            if title.hasPrefix(query) || title.replacingOccurrences(of: " ", with: "").hasPrefix(squashed) { return 0 }
            if title.split(separator: " ").contains(where: { $0.hasPrefix(query) }) { return 1 }
            if command.keywords.contains(where: { $0.hasPrefix(query) }) { return 2 }
            if title.contains(query) { return 3 }
            return nil
        }
        return allCases.compactMap { command in rank(command).map { (command, $0) } }
            .enumerated()
            .sorted { ($0.element.1, $0.offset) < ($1.element.1, $1.offset) }
            .map(\.element.0)
    }
}

/// What the open slash menu shows, driven by the text of the block it was
/// opened in. Pure, so the view only draws it.
struct SlashMenuState: Equatable {
    /// Where the `/` is in the block's text.
    let slashLocation: Int
    private(set) var query = ""
    private(set) var items = SlashCommand.allCases
    private(set) var selectedIndex = 0

    init(slashLocation: Int) {
        self.slashLocation = slashLocation
    }

    var selected: SlashCommand? {
        items.indices.contains(selectedIndex) ? items[selectedIndex] : nil
    }

    /// The `/query` text, for removing it when a command is chosen.
    var typedRange: NSRange {
        NSRange(location: slashLocation, length: 1 + (query as NSString).length)
    }

    /// Follows the text after an edit or caret move. Returns false when the
    /// menu should close: the slash was deleted, the caret left the query, or
    /// the query ends in a space and matches nothing.
    mutating func update(text: String, caret: Int) -> Bool {
        let ns = text as NSString
        guard slashLocation < ns.length, ns.character(at: slashLocation) == 0x2F,
              caret > slashLocation else { return false }
        let typed = ns.substring(with: NSRange(location: slashLocation + 1, length: caret - slashLocation - 1))
        guard !typed.contains("\n"), typed.count <= 24 else { return false }
        let matches = SlashCommand.matching(typed)
        if matches.isEmpty, typed.hasSuffix(" ") { return false }
        if typed != query {
            query = typed
            items = matches
            selectedIndex = 0
        }
        return true
    }

    mutating func moveSelection(by delta: Int) {
        guard !items.isEmpty else { return }
        selectedIndex = (selectedIndex + delta + items.count) % items.count
    }

    mutating func select(_ index: Int) {
        if items.indices.contains(index) { selectedIndex = index }
    }
}
