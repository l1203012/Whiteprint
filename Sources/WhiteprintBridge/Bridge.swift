import Foundation
import WhiteprintCore

// The app owns all state. `whiteprint-mcp` (spawned by Claude Code / Claude
// Desktop over stdio) forwards each MCP tool call to the running app as one
// BridgeRequest over a Unix domain socket and returns the reply text.
// Wire format: one JSON object per line in each direction (synthesized Codable).

/// Note ids (`n1`, `n2`, …) are assigned by the app per session and listed by
/// `listNotes`. Page numbers are 1-based. Drawing ids are per note.
public enum BridgeRequest: Codable, Equatable {
    case ping
    case listNotes
    case readNote(note: String, page: Int?)
    case createNote(title: String, markdown: String?)
    case write(note: String, page: Int, markdown: String, mode: WriteMode)
    case draw(note: String, page: Int, dsl: String, after: String?)
    case editDrawing(note: String, drawing: String, dsl: String)
    case deleteDrawing(note: String, drawing: String)
    case addPage(note: String)
    case importDocument(path: String)
    case listImports
    case readChunk(importID: String, chunk: Int)
    case savePoints(importID: String, chunk: Int, points: [StudyPoint])
    case getPoints(importID: String?)
    case buildStudyPlan(importIDs: [String], plan: StudyPlan)
}

/// Replies are short text, passed straight to Claude as the tool result.
public enum BridgeResponse: Codable, Equatable {
    case ok(String)
    case failure(String)
}

/// Implemented by the app. Called on the main queue; call `reply` exactly once,
/// from any queue.
public protocol BridgeHandler: AnyObject {
    func handle(_ request: BridgeRequest, reply: @escaping (BridgeResponse) -> Void)
}

/// Errors from the socket transport and from launching the app.
public enum BridgeError: Error, Equatable, CustomStringConvertible {
    /// Nothing is listening on the socket.
    case appNotRunning
    /// The app accepted the request but didn't reply in time.
    case timedOut(TimeInterval)
    /// Another process is already serving the socket.
    case alreadyRunning(String)
    /// `sun_path` holds at most 103 bytes plus the terminator.
    case pathTooLong(String)
    /// The app couldn't be launched, or didn't open its socket in time.
    case launchFailed(String)
    /// The connection closed early or the reply wasn't a `BridgeResponse`.
    case badReply(String)
    /// A system call failed.
    case system(String, errno: Int32)

    public var description: String {
        switch self {
        case .appNotRunning:
            return "Whiteprint isn't running"
        case let .timedOut(seconds):
            return "Whiteprint didn't reply within \(Int(seconds)) s"
        case let .alreadyRunning(path):
            return "another Whiteprint is already listening on \(path)"
        case let .pathTooLong(path):
            return "socket path is too long (max 103 bytes): \(path)"
        case let .launchFailed(reason):
            return "couldn't start Whiteprint: \(reason)"
        case let .badReply(reason):
            return "bad reply from Whiteprint: \(reason)"
        case let .system(call, code):
            return "\(call) failed: \(String(cString: strerror(code)))"
        }
    }
}

public enum BridgePaths {
    public static let appBundleID = "io.github.l1203012.whiteprint"

    /// Environment variable that overrides `socket`, for tests and debugging.
    public static let socketEnvironmentKey = "WHITEPRINT_SOCKET"

    /// `~/Library/Application Support/Whiteprint`, created on first use.
    public static var supportDirectory: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let url = base.appendingPathComponent("Whiteprint", isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    /// `supportDirectory/mcp.sock`, or `$WHITEPRINT_SOCKET` when set.
    public static var socket: URL {
        if let path = ProcessInfo.processInfo.environment[socketEnvironmentKey], !path.isEmpty {
            return URL(fileURLWithPath: path)
        }
        return supportDirectory.appendingPathComponent("mcp.sock")
    }

    /// `whiteprint-mcp` inside the running app bundle.
    public static var helperExecutable: URL {
        Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/whiteprint-mcp")
    }
}
