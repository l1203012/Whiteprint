import Foundation
import WhiteprintBridge

/// Review progress per note, deck and card, as one small JSON file per note
/// in `Application Support/Whiteprint/Flashcards`.
final class FlashcardProgressStore {
    private struct NoteProgress: Codable {
        var note: String
        /// Deck id → card key → progress.
        var decks: [String: [String: CardProgress]]
    }

    let directory: URL
    private var cache: [String: NoteProgress] = [:]

    static var defaultDirectory: URL {
        BridgePaths.supportDirectory.appendingPathComponent("Flashcards", isDirectory: true)
    }

    init(directory: URL = defaultDirectory) {
        self.directory = directory
    }

    func progress(note: URL, deck: String) -> [String: CardProgress] {
        load(note)?.decks[deck] ?? [:]
    }

    func save(_ progress: CardProgress, card: String, note: URL, deck: String) throws {
        var file = load(note) ?? NoteProgress(note: note.canonicalFile.path, decks: [:])
        file.decks[deck, default: [:]][card] = progress
        try write(file)
    }

    /// Keeps progress with a note (or every note in a folder) that moved.
    func moveNotes(from old: URL, to new: URL) {
        let files = (try? FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil)) ?? []
        for url in files where url.pathExtension == "json" {
            guard let data = try? Data(contentsOf: url),
                  var file = try? Self.decoder.decode(NoteProgress.self, from: data),
                  let moved = NoteFiles.relocated(URL(fileURLWithPath: file.note), from: old, to: new) else { continue }
            cache[url.lastPathComponent] = nil
            file.note = moved.path
            guard (try? write(file)) != nil else { continue }
            try? FileManager.default.removeItem(at: url)
        }
    }

    private static let encoder: JSONEncoder = {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = .sortedKeys
        return encoder
    }()

    private static let decoder: JSONDecoder = {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return decoder
    }()

    private func fileName(_ note: URL) -> String {
        StableHash.hex(note.canonicalFile.path) + ".json"
    }

    private func load(_ note: URL) -> NoteProgress? {
        let name = fileName(note)
        if let cached = cache[name] { return cached }
        guard let data = try? Data(contentsOf: directory.appendingPathComponent(name)),
              let file = try? Self.decoder.decode(NoteProgress.self, from: data) else { return nil }
        cache[name] = file
        return file
    }

    private func write(_ file: NoteProgress) throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let name = fileName(URL(fileURLWithPath: file.note))
        try Self.encoder.encode(file).write(to: directory.appendingPathComponent(name), options: .atomic)
        cache[name] = file
    }
}
