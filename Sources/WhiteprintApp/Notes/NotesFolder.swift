import Foundation
import WhiteprintCore

/// The folder the sidebar lists and new notes are saved to.
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
        defaults: UserDefaults = .standard,
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

    /// `.wprint` files directly inside the folder, sorted by name as Finder does.
    func noteURLs() -> [URL] {
        let contents = (try? FileManager.default.contentsOfDirectory(
            at: url, includingPropertiesForKeys: nil, options: [.skipsHiddenFiles]
        )) ?? []
        return contents
            .filter { $0.pathExtension.lowercased() == Self.fileExtension }
            .map(\.standardizedFileURL)
            .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
    }

    func contains(_ file: URL) -> Bool {
        file.standardizedFileURL.deletingLastPathComponent().path == url.standardizedFileURL.path
    }

    /// A file URL for a new note named after `title` that doesn't exist yet:
    /// `Untitled.wprint`, then `Untitled 2.wprint`, …
    func unusedURL(forTitle title: String) -> URL {
        let base = Self.fileName(forTitle: title)
        var candidate = url.appendingPathComponent(base).appendingPathExtension(Self.fileExtension)
        var n = 2
        while FileManager.default.fileExists(atPath: candidate.path) {
            candidate = url.appendingPathComponent("\(base) \(n)").appendingPathExtension(Self.fileExtension)
            n += 1
        }
        return candidate.standardizedFileURL
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

    /// Writes `note` to a new file named after `title` and returns its URL.
    func save(_ note: Note, title: String) throws -> URL {
        try create()
        let file = unusedURL(forTitle: title)
        try note.serialized().write(to: file, atomically: true, encoding: .utf8)
        return file
    }
}
