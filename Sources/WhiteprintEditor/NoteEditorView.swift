import AppKit
import WhiteprintCore
import WhiteprintRender

// CONTRACT (owner: editor agent). Public signatures are fixed; bodies are stubs.

/// The Notion-style editing surface for one note: a scrolling, centred column
/// of blue blueprint pages with white text, inline drawings, slash menu,
/// Markdown shortcuts and checkboxes.
public final class NoteEditorView: NSView {
    /// The current content, including unsaved edits.
    public private(set) var note: Note

    /// Called after each user edit (coalesced, at most every ~300 ms).
    public var onChange: ((Note) -> Void)?

    /// Called when the page nearest the top of the viewport changes (1-based).
    public var onVisiblePageChange: ((Int) -> Void)?

    public init(note: Note, palette: BlueprintPalette = .blueprint) {
        self.note = note
        super.init(frame: .zero)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    /// Replaces the content, e.g. after Claude edited the note over MCP. With
    /// `preservingSelection`, the caret and scroll position stay where they were
    /// as far as possible.
    public func setNote(_ note: Note, preservingSelection: Bool) {
        self.note = note
    }

    /// 1-based.
    public func scrollToPage(_ page: Int) {}

    /// Inserts a drawing at the caret (or the end of the current page) and
    /// opens its source popover.
    public func insertDrawingAtSelection(source: String = "") {}

    /// Adds a page after the current one and moves the caret there.
    public func addPage() {}
}
