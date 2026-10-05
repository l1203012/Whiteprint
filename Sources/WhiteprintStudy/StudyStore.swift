import CryptoKit
import Foundation
import WhiteprintCore
import WhiteprintExtract

/// Imported documents, their chunks and the points Claude saved, persisted as
/// small JSON files under `directory`. Re-importing an identical file (same
/// content hash) returns the existing import. Thread-safe.
///
/// Layout: `index.json` (id counter, imports, hashes, finished chunks),
/// `<id>/chunks/<n>.json` and `<id>/points/<n>.json`. Only the index is kept
/// in memory; chunk text and points are read from disk when asked for.
public final class StudyStore {
    /// `~/Library/Application Support/Whiteprint/Study`.
    public static var defaultDirectory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Whiteprint/Study", isDirectory: true)
    }

    public let directory: URL
    private let extractDocument: (URL) throws -> ExtractedDocument
    private let makeChunks: (ExtractedDocument) -> [ExtractedChunk]
    private let lock = NSLock()
    private var index: Index

    /// Opens (or creates) the store. `extract` and `chunk` default to the real
    /// extractor and chunker; tests inject fakes.
    public init(
        directory: URL = StudyStore.defaultDirectory,
        extract: @escaping (URL) throws -> ExtractedDocument = { try DocumentExtractor.extract($0) },
        chunk: @escaping (ExtractedDocument) -> [ExtractedChunk] = { Chunker.chunks($0) }
    ) throws {
        self.directory = directory
        extractDocument = extract
        makeChunks = chunk
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let indexURL = directory.appendingPathComponent(Self.indexFile)
        if FileManager.default.fileExists(atPath: indexURL.path) {
            index = try Self.decode(Index.self, at: Self.indexFile, in: directory)
        } else {
            index = Index()
        }
    }

    /// Extracts and chunks the file. Slow for big files: call off the main thread.
    public func importFile(_ url: URL) throws -> StudyImport {
        let hash = try Self.contentHash(of: url)
        if let existing = withLock({ index.entries.first { $0.hash == hash }?.info }) {
            return existing
        }

        let (unitCount, chunks) = try autoreleasepool { () throws -> (Int, [ExtractedChunk]) in
            let document = try extractDocument(url)
            return (document.units.count, makeChunks(document))
        }
        guard !chunks.isEmpty else { throw ExtractionError.empty(url.lastPathComponent) }

        return try withLock {
            if let existing = index.entries.first(where: { $0.hash == hash }) {
                return existing.info
            }
            var updated = index
            updated.lastID += 1
            let id = "i\(updated.lastID)"
            let folder = directory.appendingPathComponent(id, isDirectory: true)
            try? FileManager.default.removeItem(at: folder)
            for chunk in chunks {
                try write(chunk, to: chunkPath(id, chunk.index))
            }
            let info = StudyImport(
                id: id, name: url.lastPathComponent, unitCount: unitCount,
                chunkCount: chunks.count, chunksDone: 0, importedAt: Date()
            )
            updated.entries.append(Entry(info: info, hash: hash, done: []))
            try save(updated)
            return info
        }
    }

    public var imports: [StudyImport] {
        withLock { index.entries.map(\.info) }
    }

    /// Chunk text for `read_chunk`, headed with `name · chunk n/N · refs`.
    public func chunk(_ importID: String, _ n: Int) throws -> String {
        let (info, path) = try withLock { () throws -> (StudyImport, String) in
            let entry = try self.entry(importID)
            try check(n, in: entry)
            return (entry.info, chunkPath(importID, n))
        }
        let chunk = try Self.decode(ExtractedChunk.self, at: path, in: directory)
        let refs = chunk.firstRef == chunk.lastRef ? chunk.firstRef : "\(chunk.firstRef)–\(chunk.lastRef)"
        return "\(info.name) · chunk \(n)/\(info.chunkCount) · \(refs)\n\n\(chunk.text)"
    }

    /// Replaces any points saved earlier for that chunk. An empty list still
    /// marks the chunk as done. Whitespace is tidied and refs are prefixed with
    /// the file name (`slide 4` → `Lecture3.pptx · slide 4`).
    public func savePoints(_ points: [StudyPoint], importID: String, chunk: Int) throws {
        try withLock {
            guard let i = index.entries.firstIndex(where: { $0.info.id == importID }) else {
                throw StudyError.unknownImport(importID)
            }
            try check(chunk, in: index.entries[i])
            let name = index.entries[i].info.name
            try write(points.compactMap { Self.tidy($0, fileName: name) }, to: pointsPath(importID, chunk))

            var updated = index
            var done = Set(updated.entries[i].done)
            done.insert(chunk)
            updated.entries[i].done = done.sorted()
            updated.entries[i].info.chunksDone = done.count
            try save(updated)
        }
    }

    /// All saved points, of one import or of all, in document order.
    public func points(importID: String?) throws -> [StudyPoint] {
        try selectedEntries(importID).flatMap { try loadPoints($0) }
    }

    /// Compact text listing of saved points for `get_points`: one line each,
    /// `★|○|✕ text (ref) [topic]`, grouped by import.
    public func pointsSummary(importID: String?) throws -> String {
        let entries = try selectedEntries(importID)
        guard !entries.isEmpty else { return "no imports" }
        return try entries.map { entry -> String in
            var header = "\(entry.info.id) \(entry.info.name) · chunks \(entry.done.count)/\(entry.info.chunkCount)"
            let missing = stride(from: 1, through: entry.info.chunkCount, by: 1).filter { !entry.done.contains($0) }
            if !missing.isEmpty {
                header += " · not done: \(Self.ranges(missing))"
            }
            let lines = try loadPoints(entry).map(Self.summaryLine)
            return ([header] + (lines.isEmpty ? ["(no points)"] : lines)).joined(separator: "\n")
        }.joined(separator: "\n\n")
    }

    public func remove(_ importID: String) throws {
        try withLock {
            _ = try entry(importID)
            var updated = index
            updated.entries.removeAll { $0.info.id == importID }
            try save(updated)
            try? FileManager.default.removeItem(at: directory.appendingPathComponent(importID, isDirectory: true))
        }
    }

    /// Removes every import. Ids stay used, so a new import never gets an old id.
    public func removeAll() throws {
        try withLock {
            var updated = index
            let ids = updated.entries.map(\.info.id)
            updated.entries = []
            try save(updated)
            for id in ids {
                try? FileManager.default.removeItem(at: directory.appendingPathComponent(id, isDirectory: true))
            }
        }
    }
}

// MARK: - Storage

private extension StudyStore {
    struct Index: Codable {
        var lastID = 0
        var entries: [Entry] = []
    }

    struct Entry: Codable {
        var info: StudyImport
        /// SHA-256 of the file's bytes.
        var hash: String
        /// Chunk numbers with saved points, ascending.
        var done: [Int]
    }

    static let indexFile = "index.json"

    func chunkPath(_ id: String, _ n: Int) -> String { "\(id)/chunks/\(n).json" }
    func pointsPath(_ id: String, _ n: Int) -> String { "\(id)/points/\(n).json" }

    func withLock<T>(_ body: () throws -> T) rethrows -> T {
        lock.lock()
        defer { lock.unlock() }
        return try body()
    }

    func entry(_ id: String) throws -> Entry {
        guard let entry = index.entries.first(where: { $0.info.id == id }) else {
            throw StudyError.unknownImport(id)
        }
        return entry
    }

    func check(_ n: Int, in entry: Entry) throws {
        guard n >= 1 && n <= entry.info.chunkCount else {
            throw StudyError.chunkOutOfRange(n, chunkCount: entry.info.chunkCount)
        }
    }

    func selectedEntries(_ importID: String?) throws -> [Entry] {
        try withLock {
            if let importID { return [try entry(importID)] }
            return index.entries
        }
    }

    /// Reads the points of an import's finished chunks.
    func loadPoints(_ entry: Entry) throws -> [StudyPoint] {
        try entry.done.flatMap { n in
            try Self.decode([StudyPoint].self, at: pointsPath(entry.info.id, n), in: directory)
        }
    }

    /// Writes the index, then adopts it, so a failed write leaves memory and disk in step.
    func save(_ updated: Index) throws {
        try write(updated, to: Self.indexFile)
        index = updated
    }

    func write<T: Encodable>(_ value: T, to path: String) throws {
        let url = directory.appendingPathComponent(path)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try JSONEncoder().encode(value).write(to: url, options: .atomic)
    }

    static func decode<T: Decodable>(_ type: T.Type, at path: String, in directory: URL) throws -> T {
        do {
            let data = try Data(contentsOf: directory.appendingPathComponent(path))
            return try JSONDecoder().decode(type, from: data)
        } catch {
            throw StudyError.corrupt(path)
        }
    }

    /// Hashes the file in 1 MB blocks so big files don't sit in memory.
    static func contentHash(of url: URL) throws -> String {
        let handle: FileHandle
        do {
            handle = try FileHandle(forReadingFrom: url)
        } catch {
            throw ExtractionError.unreadable(url.lastPathComponent)
        }
        defer { try? handle.close() }
        var hasher = SHA256()
        while true {
            let block = try autoreleasepool { try handle.read(upToCount: 1 << 20) }
            guard let block, !block.isEmpty else { break }
            hasher.update(data: block)
        }
        return hasher.finalize().map { String(format: "%02x", $0) }.joined()
    }
}

// MARK: - Text

private extension StudyStore {
    static func tidy(_ point: StudyPoint, fileName: String) -> StudyPoint? {
        let text = oneLine(point.text)
        guard !text.isEmpty else { return nil }
        var ref = oneLine(point.ref)
        if ref.isEmpty {
            ref = fileName
        } else if !ref.hasPrefix(fileName) {
            ref = "\(fileName) · \(ref)"
        }
        let topic = point.topic.map(oneLine).flatMap { $0.isEmpty ? nil : $0 }
        return StudyPoint(text: text, importance: point.importance, ref: ref, topic: topic)
    }

    static func summaryLine(_ point: StudyPoint) -> String {
        var line = "\(point.importance.symbol) \(point.text) (\(point.ref))"
        if let topic = point.topic {
            line += " [\(topic)]"
        }
        return line
    }
}

extension StudyStore {
    /// `[1, 2, 3, 7]` → `1–3, 7`.
    static func ranges(_ numbers: [Int]) -> String {
        var parts: [String] = []
        var start = numbers[0], end = numbers[0]
        for n in numbers.dropFirst() + [Int.min] {
            if n == end + 1 {
                end = n
                continue
            }
            parts.append(start == end ? "\(start)" : "\(start)–\(end)")
            start = n
            end = n
        }
        return parts.joined(separator: ", ")
    }
}

/// Collapses runs of whitespace, including newlines, into single spaces.
func oneLine(_ text: String) -> String {
    text.split(whereSeparator: \.isWhitespace).joined(separator: " ")
}

extension Importance {
    var symbol: String {
        switch self {
        case .must: return "★"
        case .good: return "○"
        case .skip: return "✕"
        }
    }
}
