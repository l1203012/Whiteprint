import Foundation
import WhiteprintCore
import WhiteprintExtract

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
    /// A study data file couldn't be read or decoded (path relative to the store).
    case corrupt(String)
    /// `ClaudeCodeRunner.start` was called while a run is in progress.
    case alreadyRunning

    public var description: String {
        switch self {
        case .unknownImport(let id): return "no import '\(id)'"
        case let .chunkOutOfRange(n, count): return "chunk \(n) doesn't exist (import has \(count))"
        case .corrupt(let path): return "study data file '\(path)' is damaged"
        case .alreadyRunning: return "a study plan is already being generated"
        }
    }
}
