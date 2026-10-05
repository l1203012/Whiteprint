import Foundation
import WhiteprintCore

/// The folder the sidebar lists and new notes are saved to. It's a tree:
/// notes may sit in subfolders at any depth.
struct NotesFolder: Equatable {
    static let fileExtension = "wprint"
    static let defaultsKey = "NotesFolderPath"
    static let environmentKey = "WHITEPRINT_NOTES_DIR"

    let url: URL

    /// `~/Documents/Whiteprint`.
    static var defaultURL: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Whiteprint", isDirectory: true)
    }

    /// The `WHITEPRINT_NOTES_DIR` environment variable wins (handy for testing),
    /// then the folder chosen in Settings, then the default.
    static func current(
        defaults: UserDefaults = AppDefaults.store,
        environment: [String: String] = ProcessInfo.processInfo.environment
    ) -> NotesFolder {
        if let path = environment[environmentKey], !path.isEmpty {
            return NotesFolder(url: URL(fileURLWithPath: (path as NSString).expandingTildeInPath, isDirectory: true))
        }
        if let path = defaults.string(forKey: defaultsKey), !path.isEmpty {
            return NotesFolder(url: URL(fileURLWithPath: path, isDirectory: true))
        }
        return NotesFolder(url: defaultURL)
    }

    func create() throws {
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    }

    // MARK: Listing

    /// Everything under the folder: subfolders as relative paths, and notes.
    struct Listing: Equatable {
        var folders: [String] = []
        var notes: [URL] = []
    }

    /// Subfolders and `.wprint` files at any depth, hidden items skipped.
    /// Notes are sorted by folder, top level first, then by name as Finder does.
    func listing() -> Listing {
        var listing = Listing()
        let keys: [URLResourceKey] = [.isDirectoryKey]
        guard let enumerator = FileManager.default.enumerator(
            at: url, includingPropertiesForKeys: keys, options: [.skipsHiddenFiles, .skipsPackageDescendants]
        ) else { return listing }
        for case let item as URL in enumerator {
            let isDirectory = (try? item.resourceValues(forKeys: Set(keys)).isDirectory) ?? false
            if isDirectory {
                if let path = relativePath(of: item) { listing.folders.append(path) }
            } else if item.pathExtension.lowercased() == Self.fileExtension {
                listing.notes.append(item.canonicalFile)
            }
        }
        listing.folders.sort { Self.precedes($0, $1) }
        listing.notes.sort { a, b in
            let (fa, fb) = (relativeFolder(of: a) ?? "", relativeFolder(of: b) ?? "")
            if fa != fb { return Self.precedes(fa, fb) }
            return Self.precedes(a.lastPathComponent, b.lastPathComponent)
        }
        return listing
    }

    /// `.wprint` files at any depth, in listing order.
    func noteURLs() -> [URL] {
        listing().notes
    }

    /// Whether `file` is anywhere inside the folder.
    func contains(_ file: URL) -> Bool {
        relativePath(of: file).map { !$0.isEmpty } ?? false
    }

    /// `Courses/Networks` for `<root>/Courses/Networks`, `""` for the root
    /// itself, nil for anything outside.
    func relativePath(of item: URL) -> String? {
        let root = url.canonicalFile.pathComponents
        let parts = item.canonicalFile.pathComponents
        guard parts.count >= root.count, Array(parts.prefix(root.count)) == root else { return nil }
        return parts.dropFirst(root.count).joined(separator: "/")
    }

    /// The folder a note is in, relative to the root (`""` at the top level),
    /// or nil when the note is saved elsewhere.
    func relativeFolder(of file: URL) -> String? {
        relativePath(of: file.canonicalFile.deletingLastPathComponent())
    }

    static func precedes(_ a: String, _ b: String) -> Bool {
        a.localizedStandardCompare(b) == .orderedAscending
    }

    // MARK: Subfolders

    enum FolderError: Error, CustomStringConvertible {
        case notRelative(String)

        var description: String {
            switch self {
            case .notRelative(let path):
                return "folder '\(path)' must be a path inside the notes folder, like Courses/Networks (no '..' or leading '/')"
            }
        }
    }

    /// The URL of the subfolder at `path` (`Courses/Networks`). Only relative
    /// paths that stay inside the folder are accepted; empty means the root.
    func folderURL(forPath path: String) throws -> URL {
        let trimmed = path.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.hasPrefix("/"), !trimmed.hasPrefix("~") else { throw FolderError.notRelative(path) }
        let parts = trimmed.split(separator: "/").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        guard !parts.contains(where: { $0 == ".." || $0 == "." || $0.contains(":") }) else {
            throw FolderError.notRelative(path)
        }
        return parts.reduce(url.canonicalFile) { $0.appendingPathComponent($1, isDirectory: true) }
    }

    /// Creates `New Folder` (or `New Folder 2`, …) inside `parent`.
    func createFolder(named name: String = "New Folder", in parent: URL? = nil) throws -> URL {
        let parent = parent ?? url
        let folder = Self.unusedURL(base: Self.fileName(forTitle: name), extension: nil, in: parent)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder.canonicalFile
    }

    // MARK: New notes

    /// A file URL for a new note named after `title` that doesn't exist yet:
    /// `Untitled.wprint`, then `Untitled 2.wprint`, …
    func unusedURL(forTitle title: String, in folder: URL? = nil) -> URL {
        Self.unusedURL(base: Self.fileName(forTitle: title), extension: Self.fileExtension, in: folder ?? url).canonicalFile
    }

    /// `<base>.<ext>` in `directory`, counting up while the name is taken.
    static func unusedURL(base: String, extension ext: String?, in directory: URL) -> URL {
        func candidate(_ name: String) -> URL {
            let file = directory.appendingPathComponent(name, isDirectory: ext == nil)
            return ext.map { file.appendingPathExtension($0) } ?? file
        }
        var url = candidate(base)
        var n = 2
        while FileManager.default.fileExists(atPath: url.path) {
            url = candidate("\(base) \(n)")
            n += 1
        }
        return url
    }

    /// A safe file name for `title`: no path separators, colons or leading dots,
    /// at most 100 characters, `Untitled` when nothing is left.
    static func fileName(forTitle title: String) -> String {
        let cleaned = title
            .components(separatedBy: CharacterSet(charactersIn: "/:\\").union(.newlines).union(.controlCharacters))
            .joined(separator: "-")
            .trimmingCharacters(in: .whitespaces)
        let trimmed = String(cleaned.drop { $0 == "." }.prefix(100)).trimmingCharacters(in: .whitespaces)
        return trimmed.isEmpty ? "Untitled" : trimmed
    }

    /// Writes `note` to a new file named after `title` in `folder` (default:
    /// the root, created if needed) and returns its URL.
    func save(_ note: Note, title: String, in folder: URL? = nil) throws -> URL {
        try FileManager.default.createDirectory(at: folder ?? url, withIntermediateDirectories: true)
        let file = unusedURL(forTitle: title, in: folder)
        try note.serialized().write(to: file, atomically: true, encoding: .utf8)
        return file
    }
}
