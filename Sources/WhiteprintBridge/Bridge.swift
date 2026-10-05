import Foundation
import WhiteprintCore

// CONTRACT (owner: bridge agent). Public signatures are fixed; bodies are stubs.
//
// The app owns all state. `whiteprint-mcp` (spawned by Claude Code / Claude
// Desktop over stdio) forwards each MCP tool call to the running app as one
// BridgeRequest over a Unix domain socket and returns the reply text.
// Wire format: one JSON object per line in each direction.

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

/// Listens on a Unix domain socket and dispatches requests to the handler.
/// Removes a stale socket file on start, and the socket file on stop.
public final class BridgeServer {
    public init(socketURL: URL = BridgePaths.socket, handler: BridgeHandler) {}
    public func start() throws {}
    public func stop() {}
}

/// Used by `whiteprint-mcp`. Blocking; one request at a time.
public final class BridgeClient {
    public init(socketURL: URL = BridgePaths.socket) {}

    public func send(_ request: BridgeRequest, timeout: TimeInterval = 120) throws -> BridgeResponse {
        .failure("not implemented")
    }
}

public enum BridgePaths {
    public static let appBundleID = "io.github.l1203012.whiteprint"

    /// `~/Library/Application Support/Whiteprint`, created on first use.
    public static var supportDirectory: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let url = base.appendingPathComponent("Whiteprint", isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    public static var socket: URL {
        supportDirectory.appendingPathComponent("mcp.sock")
    }

    /// `whiteprint-mcp` inside the running app bundle.
    public static var helperExecutable: URL {
        Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/whiteprint-mcp")
    }
}

/// The MCP server run by `whiteprint-mcp`: JSON-RPC 2.0 over stdio
/// (initialize, tools/list, tools/call, resources/list, resources/read,
/// prompts/list, prompts/get, ping). Lives in the library so it's testable.
public final class MCPServer {
    /// `send` forwards a request to the app (normally `BridgeClient.send`,
    /// after making sure the app is running).
    public init(send: @escaping (BridgeRequest) throws -> BridgeResponse) {}

    /// Handles one JSON-RPC message; returns the response line, or nil for notifications.
    public func handle(line: String) -> String? {
        nil
    }

    /// Reads stdin line by line until EOF, writing responses to stdout.
    public func run() {}
}
