import AppKit
import CoreServices
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
    /// Relative to the notes folder (`Courses/Networks`), `""` at the top
    /// level, nil for an open note saved elsewhere.
    var folder: String?
    var decks: [CardDeck] = []
}

/// The notes folder tree and the open documents, kept as one list that
/// refreshes when files change on disk or a document's title changes.
final class NotesLibrary {
    private(set) var folder: NotesFolder
    private(set) var entries: [NoteEntry] = []
    /// Every subfolder, relative to the notes folder, empty ones included.
    private(set) var folders: [String] = []
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
        AppDefaults.store.set(url.path, forKey: NotesFolder.defaultsKey)
        folder = NotesFolder(url: url)
        startWatching()
        reload()
    }

    /// Notes-folder files, then open documents saved elsewhere.
    var urls: [URL] {
        Self.merged(folder.noteURLs(), open: NoteDocuments.open.compactMap { $0.fileURL?.canonicalFile })
    }

    static func merged(_ inFolder: [URL], open: [URL]) -> [URL] {
        let known = Set(inFolder.map(\.path))
        return inFolder + open.filter { !known.contains($0.path) }
    }

    func entry(for url: URL) -> NoteEntry? {
        let url = url.canonicalFile
        return entries.first { $0.url == url }
    }

    func reload() {
        let listing = folder.listing()
        let all = Self.merged(listing.notes, open: NoteDocuments.open.compactMap { $0.fileURL?.canonicalFile })
        let fresh = all.map { url -> NoteEntry in
            let note = NoteDocuments.document(for: url)?.note ?? (try? Self.read(url))
            return NoteEntry(
                url: url,
                title: note.map { NoteTitle.display(for: $0, fileURL: url) } ?? NoteTitle.baseName(url),
                pageCount: note?.pages.count ?? 0,
                folder: folder.relativeFolder(of: url),
                decks: note?.decks ?? []
            )
        }
        guard fresh != entries || listing.folders != folders else { return }
        entries = fresh
        folders = listing.folders
        NotificationCenter.default.post(name: .notesLibraryDidChange, object: self)
    }

    /// Refreshes one open document's entry without touching the disk.
    private func update(_ document: NoteDocument) {
        guard let url = document.fileURL?.canonicalFile,
              let i = entries.firstIndex(where: { $0.url == url }) else { return reload() }
        var entry = entries[i]
        entry.title = document.title
        entry.pageCount = document.note.pages.count
        entry.decks = document.note.decks
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

/// Calls `onChange` on the main queue (coalesced over a quarter second) when
/// anything changes in a folder or any of its subfolders (FSEvents).
final class FolderWatcher {
    private var stream: FSEventStreamRef?
    private let onChange: () -> Void

    init(url: URL, onChange: @escaping () -> Void) {
        self.onChange = onChange
        var context = FSEventStreamContext(
            version: 0, info: Unmanaged.passUnretained(self).toOpaque(), retain: nil, release: nil, copyDescription: nil
        )
        let callback: FSEventStreamCallback = { _, info, _, _, _, _ in
            guard let info else { return }
            Unmanaged<FolderWatcher>.fromOpaque(info).takeUnretainedValue().onChange()
        }
        guard let stream = FSEventStreamCreate(
            nil, callback, &context, [url.path] as CFArray,
            FSEventStreamEventId(kFSEventStreamEventIdSinceNow), 0.25, FSEventStreamCreateFlags(kFSEventStreamCreateFlagNone)
        ) else { return }
        FSEventStreamSetDispatchQueue(stream, .main)
        FSEventStreamStart(stream)
        self.stream = stream
    }

    deinit {
        guard let stream else { return }
        FSEventStreamStop(stream)
        FSEventStreamInvalidate(stream)
        FSEventStreamRelease(stream)
    }
}
