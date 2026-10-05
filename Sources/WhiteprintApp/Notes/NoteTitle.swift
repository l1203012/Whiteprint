import Foundation
import WhiteprintCore

/// How a note is named in the window title, sidebar and `list_notes`.
enum NoteTitle {
    static let untitled = "Untitled"

    /// The front matter `title`, falling back to the file name.
    static func display(for note: Note, fileURL: URL?) -> String {
        if let title = note.frontMatter.title?.trimmingCharacters(in: .whitespaces), !title.isEmpty {
            return title
        }
        return fileURL.map(baseName) ?? untitled
    }

    /// The new `title` after the file moved from `old` to `new`, or nil when
    /// the file kept its name (a save, a move to another folder).
    static func titleAfterRename(from old: URL?, to new: URL?) -> String? {
        guard let old, let new else { return nil }
        let newName = baseName(new)
        return baseName(old) == newName ? nil : newName
    }

    static func baseName(_ url: URL) -> String {
        url.deletingPathExtension().lastPathComponent
    }
}
