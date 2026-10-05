import AppKit
import WhiteprintCore

extension Notification.Name {
    /// Posted on the main queue when the notes list or the notes folder changes.
    static let notesLibraryDidChange = Notification.Name("WhiteprintNotesLibraryDidChange")
}

/// A note as listed in the sidebar, the palette and `list_notes`.
struct NoteEntry: Equatable {
    var url: URL
    var title: String
    var pageCount: Int
}

/// The notes folder and the open documents, kept as one list that refreshes
/// when files change on disk or a document's title changes.
final class NotesLibrary {
    private(set) var folder: NotesFolder
    private(set) var entries: [NoteEntry] = []
    private var watcher: FolderWatcher?
    private var observers: [NSObjectProtocol] = []

    init(folder: NotesFolder = .current()) {
        self.folder = folder
        let center = NotificationCenter.default
        observers = [
            center.addObserver(forName: .noteDocumentDidChange, object: nil, queue: .main) { [weak self] notification in
                (notification.object as? NoteDocument).map { self?.update($0) }
            },
            center.addObserver(forName: .noteDocumentDidMove, object: nil, queue: .main) { [weak self] _ in
                self?.reload()
            },
        ]
        startWatching()
        reload()
    }

    deinit {
        observers.forEach(NotificationCenter.default.removeObserver)
    }

    /// Switches to another folder (from Settings) and remembers it.
    func changeFolder(to url: URL) {
        UserDefaults.standard.set(url.path, forKey: NotesFolder.defaultsKey)
        folder = NotesFolder(url: url)
        startWatching()
        reload()
    }

    /// Notes-folder files, then open documents saved elsewhere.
    var urls: [URL] {
        let inFolder = folder.noteURLs()
        let known = Set(inFolder.map(\.path))
        let elsewhere = NoteDocuments.open.compactMap { $0.fileURL?.standardizedFileURL }.filter { !known.contains($0.path) }
        return inFolder + elsewhere
    }

    func reload() {
        let fresh = urls.map { url -> NoteEntry in
            let note = NoteDocuments.document(for: url)?.note ?? (try? Self.read(url))
            return NoteEntry(
                url: url,
                title: note.map { NoteTitle.display(for: $0, fileURL: url) } ?? NoteTitle.baseName(url),
                pageCount: note?.pages.count ?? 0
            )
        }
        guard fresh != entries else { return }
        entries = fresh
        NotificationCenter.default.post(name: .notesLibraryDidChange, object: self)
    }

    /// Refreshes one open document's entry without touching the disk.
    private func update(_ document: NoteDocument) {
        guard let url = document.fileURL?.standardizedFileURL,
              let i = entries.firstIndex(where: { $0.url == url }) else { return reload() }
        let entry = NoteEntry(url: url, title: document.title, pageCount: document.note.pages.count)
        guard entries[i] != entry else { return }
        entries[i] = entry
        NotificationCenter.default.post(name: .notesLibraryDidChange, object: self)
    }

    static func read(_ url: URL) throws -> Note {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            throw WorkspaceError.unreadable(url.lastPathComponent)
        }
        return try Note(parsing: text)
    }

    private func startWatching() {
        try? folder.create()
        watcher = FolderWatcher(url: folder.url) { [weak self] in self?.reload() }
    }
}

/// Calls `onChange` on the main queue (coalesced) when files are added,
/// removed, renamed or written in a folder.
final class FolderWatcher {
    private let source: DispatchSourceFileSystemObject?
    private var pending = false

    init(url: URL, onChange: @escaping () -> Void) {
        let fd = open(url.path, O_EVTONLY)
        guard fd >= 0 else {
            source = nil
            return
        }
        let source = DispatchSource.makeFileSystemObjectSource(fileDescriptor: fd, eventMask: [.write, .rename, .delete, .extend], queue: .main)
        self.source = source
        source.setEventHandler { [weak self] in
            guard let self, !self.pending else { return }
            self.pending = true
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                self.pending = false
                onChange()
            }
        }
        source.setCancelHandler { close(fd) }
        source.resume()
    }

    deinit {
        source?.cancel()
    }
}
