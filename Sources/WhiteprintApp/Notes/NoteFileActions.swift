import Foundation
import WhiteprintCore

/// Sidebar file operations with their side effects: open documents follow
/// their files, Claude's note ids and flashcard progress move along, and the
/// library refreshes.
enum NoteFileActions {
    static func move(_ items: [URL], into folder: URL) throws {
        defer { AppServices.shared.library.reload() }
        for item in items {
            let moved = try NoteFiles.move(item, into: folder)
            if moved.path != item.canonicalFile.path { AppServices.shared.itemDidMove(from: item, to: moved) }
        }
    }

    /// Renames a folder or a note. A note's title follows its new name; an
    /// open note's document takes care of that itself.
    @discardableResult
    static func rename(_ item: URL, to name: String) throws -> URL {
        let wasOpen = !NoteDocuments.documents(at: item).isEmpty
        let renamed = try NoteFiles.rename(item, to: name)
        guard renamed.path != item.canonicalFile.path else { return renamed }
        AppServices.shared.itemDidMove(from: item, to: renamed)
        if !wasOpen, renamed.pathExtension.lowercased() == NotesFolder.fileExtension,
           var note = try? NotesLibrary.read(renamed) {
            note.frontMatter.title = NoteTitle.baseName(renamed)
            try note.serialized().write(to: renamed, atomically: true, encoding: .utf8)
            AppServices.shared.library.reload()
        }
        return renamed
    }

    /// Closes the notes open from `item`, then moves it to the Trash.
    static func trash(_ item: URL) throws {
        NoteDocuments.documents(at: item).forEach { $0.close() }
        try NoteFiles.trash(item)
        AppServices.shared.library.reload()
    }
}
