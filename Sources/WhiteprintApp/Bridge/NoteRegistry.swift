import Foundation

/// Session ids for notes (`n1`, `n2`, …) as Claude sees them. An id stays
/// bound to its file for the whole session, and follows it when it's renamed.
final class NoteRegistry {
    private var idsByPath: [String: String] = [:]
    private var urlsByID: [String: URL] = [:]
    private var next = 1

    func id(for url: URL) -> String {
        let url = url.standardizedFileURL
        if let id = idsByPath[url.path] { return id }
        let id = "n\(next)"
        next += 1
        idsByPath[url.path] = id
        urlsByID[id] = url
        return id
    }

    func url(for id: String) -> URL? {
        urlsByID[id.trimmingCharacters(in: .whitespaces).lowercased()]
    }

    /// Keeps the id of a file that was renamed or moved.
    func move(from old: URL, to new: URL) {
        let old = old.standardizedFileURL, new = new.standardizedFileURL
        guard old != new, let id = idsByPath.removeValue(forKey: old.path) else { return }
        idsByPath[new.path] = id
        urlsByID[id] = new
    }
}
