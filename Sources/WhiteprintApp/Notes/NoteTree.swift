import Foundation

/// The notes folder as the sidebar shows it: in each folder, subfolders
/// first (alphabetical), then notes in listing order.
struct NoteTree: Equatable {
    var name: String
    /// Relative to the notes folder; `""` for the root.
    var path: String
    var folders: [NoteTree] = []
    var notes: [NoteEntry] = []

    /// Notes saved outside the notes folder (`folder == nil`) go last in the root.
    static func build(entries: [NoteEntry], folders: [String]) -> NoteTree {
        var root = NoteTree(name: "", path: "")
        for path in folders {
            root.insertFolder(path.split(separator: "/").map(String.init))
        }
        var elsewhere: [NoteEntry] = []
        for entry in entries {
            guard let folder = entry.folder else {
                elsewhere.append(entry)
                continue
            }
            root.insert(entry, at: folder.split(separator: "/").map(String.init))
        }
        root.notes += elsewhere
        root.sortFolders()
        return root
    }

    private mutating func insertFolder(_ parts: [String]) {
        guard let first = parts.first else { return }
        let index = childIndex(first)
        folders[index].insertFolder(Array(parts.dropFirst()))
    }

    private mutating func insert(_ entry: NoteEntry, at parts: [String]) {
        guard let first = parts.first else { return notes.append(entry) }
        let index = childIndex(first)
        folders[index].insert(entry, at: Array(parts.dropFirst()))
    }

    private mutating func childIndex(_ name: String) -> Int {
        if let index = folders.firstIndex(where: { $0.name == name }) { return index }
        folders.append(NoteTree(name: name, path: path.isEmpty ? name : path + "/" + name))
        return folders.count - 1
    }

    private mutating func sortFolders() {
        folders.sort { NotesFolder.precedes($0.name, $1.name) }
        for i in folders.indices { folders[i].sortFolders() }
    }
}
