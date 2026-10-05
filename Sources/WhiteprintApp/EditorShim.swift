// TEMPORARY until the editor lands — the lead deletes this file when merging.
// Declares the NoteEditorView API the editor agent is adding, so the app compiles.
import AppKit
import ObjectiveC
import WhiteprintCore
import WhiteprintEditor

public enum PageLayoutMode: String, CaseIterable {
    case slides, a4
}

public enum EditorCommand: String, CaseIterable {
    case heading1, heading2, heading3, body, bold, italic, inlineCode, bulletList, numberedList, checklist, quote, codeBlock, divider, drawing, flashcards, newPage
}

private var layoutModeKey = 0
private var syntaxKey = 0
private var studyDeckKey = 0

extension NoteEditorView {
    var layoutMode: PageLayoutMode {
        get { objc_getAssociatedObject(self, &layoutModeKey) as? PageLayoutMode ?? .slides }
        set { objc_setAssociatedObject(self, &layoutModeKey, newValue, .OBJC_ASSOCIATION_RETAIN) }
    }

    var showsMarkdownSyntax: Bool {
        get { objc_getAssociatedObject(self, &syntaxKey) as? Bool ?? false }
        set { objc_setAssociatedObject(self, &syntaxKey, newValue, .OBJC_ASSOCIATION_RETAIN) }
    }

    var onStudyDeck: ((CardDeck) -> Void)? {
        get { objc_getAssociatedObject(self, &studyDeckKey) as? (CardDeck) -> Void }
        set { objc_setAssociatedObject(self, &studyDeckKey, newValue, .OBJC_ASSOCIATION_RETAIN) }
    }

    func perform(_ command: EditorCommand) {}

    func insertDeckAtSelection() {}
}
