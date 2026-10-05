import Foundation

/// Session ids for notes (`n1`, `n2`, …) as Claude sees them. An id stays
/// bound to its file for the whole session, and follows it when it's renamed.
final class NoteRegistry {
    private var idsByPath: [String: String] = [:]
    private var urlsByID: [String: URL] = [:]
    private var next = 1

    func id(for url: URL) -> String {
        let url = url.canonicalFile
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

    /// Keeps the ids of a file that was renamed or moved, or of every note
    /// in a folder that was.
    func move(from old: URL, to new: URL) {
        guard old.canonicalFile != new.canonicalFile else { return }
        for (id, url) in urlsByID {
            guard let moved = NoteFiles.relocated(url, from: old, to: new) else { continue }
            idsByPath[url.path] = nil
            idsByPath[moved.path] = id
            urlsByID[id] = moved
        }
    }
}
