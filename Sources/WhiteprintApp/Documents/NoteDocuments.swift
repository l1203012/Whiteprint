import AppKit
import WhiteprintCore

/// Finding and opening note documents through the shared document controller.
enum NoteDocuments {
    static var open: [NoteDocument] {
        NSDocumentController.shared.documents.compactMap { $0 as? NoteDocument }
    }

    static func document(for url: URL) -> NoteDocument? {
        let url = url.canonicalFile
        return open.first { $0.fileURL?.canonicalFile == url }
    }

    /// The frontmost note window, even while Whiteprint isn't the active app.
    static var frontWindow: NSWindow? {
        NSApp.mainWindow.flatMap { $0.windowController is NoteWindowController ? $0 : nil }
            ?? NSApp.orderedWindows.first { $0.windowController is NoteWindowController && $0.isVisible }
    }

    /// Opens `url` synchronously (so a bridge request can edit it right
    /// away) and brings its window to the front.
    @discardableResult
    static func show(_ url: URL) throws -> NoteDocument {
        let controller = NSDocumentController.shared
        let document: NoteDocument
        if let existing = self.document(for: url) {
            document = existing
        } else {
            guard let made = try controller.makeDocument(withContentsOf: url, ofType: NoteDocument.typeName) as? NoteDocument else {
                throw WorkspaceError.unreadable(url.lastPathComponent)
            }
            controller.addDocument(made)
            made.makeWindowControllers()
            controller.noteNewRecentDocument(made)
            document = made
        }
        document.showWindows()
        return document
    }

    /// Opens `url` the usual asynchronous way, reporting errors to the user.
    static func open(_ url: URL) {
        NSDocumentController.shared.openDocument(withContentsOf: url, display: true) { _, _, error in
            if let error, (error as NSError).code != NSUserCancelledError { NSApp.presentError(error) }
        }
    }
}

/// The app's `NoteWorkspace`: the notes library plus NSDocuments.
final class DocumentWorkspace: NoteWorkspace {
    private let library: NotesLibrary

    init(library: NotesLibrary) {
        self.library = library
    }

    func noteURLs() -> [URL] {
        library.urls
    }

    func note(at url: URL) throws -> Note {
        if let document = NoteDocuments.document(for: url) { return document.note }
        guard FileManager.default.fileExists(atPath: url.path) else {
            throw WorkspaceError.fileNotFound(url.path)
        }
        return try NotesLibrary.read(url)
    }

    func edit<T>(noteAt url: URL, actionName: String, _ change: (inout Note) throws -> T) throws -> T {
        guard NoteDocuments.document(for: url) != nil || FileManager.default.fileExists(atPath: url.path) else {
            throw WorkspaceError.fileNotFound(url.path)
        }
        return try NoteDocuments.show(url).apply(actionName: actionName, change)
    }

    func createNote(_ note: Note, title: String) throws -> URL {
        let url = try library.folder.save(note, title: title)
        try NoteDocuments.show(url)
        library.reload()
        return url
    }
}
