import Foundation

/// Moves, renames and trashes notes and folders through `NSFileCoordinator`,
/// so open documents (file presenters) hear about it.
enum NoteFiles {
    enum FileError: Error, CustomStringConvertible {
        case intoItself(String)
        case nameTaken(String)
        case badName(String)

        var description: String {
            switch self {
            case .intoItself(let name): return "“\(name)” can’t be moved into itself."
            case .nameTaken(let name): return "An item named “\(name)” already exists here."
            case .badName(let name): return "“\(name)” can’t be used as a name."
            }
        }
    }

    /// Moves a note or folder into `folder`, keeping its name (or counting up
    /// when it's taken). Returns the new URL; unchanged when it's already there.
    @discardableResult
    static func move(_ item: URL, into folder: URL) throws -> URL {
        let item = item.canonicalFile, folder = folder.canonicalFile
        if item.deletingLastPathComponent().path == folder.path { return item }
        if isInside(folder, item) { throw FileError.intoItself(item.lastPathComponent) }
        let isFolder = item.hasDirectoryPath || isDirectory(item)
        let destination = isFolder || item.pathExtension.isEmpty
            ? NotesFolder.unusedURL(base: item.lastPathComponent, extension: nil, in: folder)
            : NotesFolder.unusedURL(base: item.deletingPathExtension().lastPathComponent, extension: item.pathExtension, in: folder)
        try coordinatedMove(item, to: destination)
        return destination.canonicalFile
    }

    /// Renames a note (keeping its extension) or a folder. Returns the new URL.
    @discardableResult
    static func rename(_ item: URL, to name: String) throws -> URL {
        let item = item.canonicalFile
        guard !name.trimmingCharacters(in: .whitespaces).isEmpty else { throw FileError.badName(name) }
        let clean = NotesFolder.fileName(forTitle: name)
        let isFolder = item.hasDirectoryPath || isDirectory(item)
        var destination = item.deletingLastPathComponent().appendingPathComponent(clean, isDirectory: isFolder)
        if !isFolder, !item.pathExtension.isEmpty { destination.appendPathExtension(item.pathExtension) }
        if destination.lastPathComponent == item.lastPathComponent { return item }
        let caseOnly = destination.lastPathComponent.lowercased() == item.lastPathComponent.lowercased()
        if !caseOnly, FileManager.default.fileExists(atPath: destination.path) {
            throw FileError.nameTaken(destination.lastPathComponent)
        }
        try coordinatedMove(item, to: destination)
        return destination.canonicalFile
    }

    /// Moves a note or folder to the Trash.
    static func trash(_ item: URL) throws {
        var coordinationError: NSError?
        var thrown: Error?
        NSFileCoordinator().coordinate(writingItemAt: item, options: .forDeleting, error: &coordinationError) { url in
            do { try FileManager.default.trashItem(at: url, resultingItemURL: nil) } catch { thrown = error }
        }
        if let error = coordinationError ?? thrown { throw error }
    }

    /// Whether `item` is `folder` or somewhere inside it.
    static func isInside(_ item: URL, _ folder: URL) -> Bool {
        let parts = item.canonicalFile.pathComponents, root = folder.canonicalFile.pathComponents
        return parts.count >= root.count && Array(parts.prefix(root.count)) == root
    }

    /// `item` with its `old` prefix replaced by `new`, or nil when it isn't inside `old`.
    static func relocated(_ item: URL, from old: URL, to new: URL) -> URL? {
        let parts = item.canonicalFile.pathComponents, root = old.canonicalFile.pathComponents
        guard isInside(item, old) else { return nil }
        return parts.dropFirst(root.count).reduce(new.canonicalFile) { $0.appendingPathComponent($1) }
    }

    private static func isDirectory(_ url: URL) -> Bool {
        var isDirectory: ObjCBool = false
        return FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory) && isDirectory.boolValue
    }

    private static func coordinatedMove(_ source: URL, to destination: URL) throws {
        let coordinator = NSFileCoordinator()
        var coordinationError: NSError?
        var thrown: Error?
        coordinator.coordinate(
            writingItemAt: source, options: .forMoving,
            writingItemAt: destination, options: .forReplacing, error: &coordinationError
        ) { from, to in
            do {
                try FileManager.default.moveItem(at: from, to: to)
                coordinator.item(at: from, didMoveTo: to)
            } catch {
                thrown = error
            }
        }
        if let error = coordinationError ?? thrown { throw error }
    }
}
