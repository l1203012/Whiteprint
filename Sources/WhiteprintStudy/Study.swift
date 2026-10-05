import Foundation
import WhiteprintCore
import WhiteprintExtract

// CONTRACT (owner: study agent). Public signatures are fixed; bodies are stubs.

public struct StudyImport: Codable, Equatable, Identifiable {
    /// `i1`, `i2`, … never reused.
    public var id: String
    /// File name, e.g. `Lecture3.pptx`.
    public var name: String
    /// Pages, slides or sections found.
    public var unitCount: Int
    public var chunkCount: Int
    /// How many chunks have saved points.
    public var chunksDone: Int
    public var importedAt: Date

    public init(id: String, name: String, unitCount: Int, chunkCount: Int, chunksDone: Int, importedAt: Date) {
        self.id = id
        self.name = name
        self.unitCount = unitCount
        self.chunkCount = chunkCount
        self.chunksDone = chunksDone
        self.importedAt = importedAt
    }
}

public enum StudyError: Error, Equatable, CustomStringConvertible {
    case unknownImport(String)
    case chunkOutOfRange(Int, chunkCount: Int)

    public var description: String {
        switch self {
        case .unknownImport(let id): return "no import '\(id)'"
        case let .chunkOutOfRange(n, count): return "chunk \(n) doesn't exist (import has \(count))"
        }
    }
}

/// Imported documents, their chunks and the points Claude saved, persisted as
/// small JSON files under `directory`. Re-importing an identical file (same
/// content hash) returns the existing import. Thread-safe.
public final class StudyStore {
    /// `~/Library/Application Support/Whiteprint/Study`.
    public static var defaultDirectory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Whiteprint/Study", isDirectory: true)
    }

    public init(directory: URL = StudyStore.defaultDirectory) throws {}

    /// Extracts and chunks the file. Slow for big files: call off the main thread.
    public func importFile(_ url: URL) throws -> StudyImport {
        throw StudyError.unknownImport(url.lastPathComponent)
    }

    public var imports: [StudyImport] { [] }

    /// Chunk text for `read_chunk`, headed with `name · chunk n/N · refs`.
    public func chunk(_ importID: String, _ n: Int) throws -> String {
        throw StudyError.unknownImport(importID)
    }

    /// Replaces any points saved earlier for that chunk.
    public func savePoints(_ points: [StudyPoint], importID: String, chunk: Int) throws {}

    /// All saved points, of one import or of all.
    public func points(importID: String?) throws -> [StudyPoint] { [] }

    /// Compact text listing of saved points for `get_points`: one line each,
    /// `★|○|✕ text (ref) [topic]`, grouped by import.
    public func pointsSummary(importID: String?) throws -> String { "" }

    public func remove(_ importID: String) throws {}
    public func removeAll() throws {}
}

public enum StudyPlanRenderer {
    /// The study plan as a Whiteprint note: overview, learning path with ★/○/✕
    /// tiers and refs, a `- [ ]` to-do checklist, and diagrams as drawings.
    public static func note(for plan: StudyPlan) -> Note {
        Note(title: plan.title)
    }
}

/// Runs the user's own logged-in Claude Code headlessly (`claude -p`) with
/// the Whiteprint MCP server, so the study plan uses their subscription.
public final class ClaudeCodeRunner {
    public enum Event: Equatable {
        /// Short human-readable progress, e.g. "Reading chunk 3 of 12".
        case status(String)
        /// Assistant text, streamed.
        case text(String)
        case finished
        case failed(String)
    }

    /// Looks for `claude` on the login shell's PATH and in the usual install
    /// locations (`~/.local/bin`, `/opt/homebrew/bin`, `/usr/local/bin`).
    public static func locateClaude() -> URL? { nil }

    public init(claudeURL: URL) {}

    public var isRunning: Bool { false }

    /// Starts `claude -p <prompt> --mcp-config <file> --allowedTools mcp__whiteprint__*
    /// --output-format stream-json --verbose` for the given imports. Events are
    /// delivered on the main queue.
    public func start(importIDs: [String], helperURL: URL, onEvent: @escaping (Event) -> Void) throws {}

    public func cancel() {}
}
