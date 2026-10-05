import Foundation
import WhiteprintCore

/// The MCP server run by `whiteprint-mcp`: JSON-RPC 2.0 over stdio
/// (initialize, tools/list, tools/call, resources/list, resources/read,
/// prompts/list, prompts/get, ping). Lives in the library so it's testable.
///
/// Messages are newline-delimited. stdout carries only protocol lines; logs go
/// to stderr. Tool failures are results with `isError`, not JSON-RPC errors.
public final class MCPServer {
    static let protocolVersions = ["2025-06-18", "2025-03-26", "2024-11-05"]
    static let dslURI = "whiteprint://dsl"
    static let instructions = """
    Whiteprint is a notes app. Get note ids from list_notes; pages are 1-based. \
    Read resource \(dslURI) before your first draw.
    """

    private let send: (BridgeRequest) throws -> BridgeResponse

    /// `send` forwards a request to the app (normally `BridgeClient.send`,
    /// after making sure the app is running).
    public init(send: @escaping (BridgeRequest) throws -> BridgeResponse) {
        self.send = send
    }

    /// Handles one JSON-RPC message; returns the response line, or nil for notifications.
    public func handle(line: String) -> String? {
        guard let message = try? JSONSerialization.jsonObject(with: Data(line.utf8), options: .fragmentsAllowed) else {
            return encode(Self.error(id: NSNull(), code: -32700, "parse error"))
        }
        if let batch = message as? [Any] {
            guard !batch.isEmpty else { return encode(Self.error(id: NSNull(), code: -32600, "empty batch")) }
            let replies = batch.compactMap(respond(to:))
            return replies.isEmpty ? nil : encode(replies)
        }
        return respond(to: message).flatMap(encode)
    }

    /// Reads stdin line by line until EOF, writing responses to stdout.
    public func run() {
        while let line = readLine(strippingNewline: true) {
            guard !line.trimmingCharacters(in: .whitespaces).isEmpty, let reply = handle(line: line) else { continue }
            FileHandle.standardOutput.write(Data((reply + "\n").utf8))
        }
    }

    private func respond(to message: Any) -> [String: Any]? {
        guard let object = message as? [String: Any] else {
            return Self.error(id: NSNull(), code: -32600, "invalid request")
        }
        let id = object["id"]
        guard object["jsonrpc"] as? String == "2.0", let method = object["method"] as? String else {
            // A response from the client (we never send requests) needs no reply.
            if object["method"] == nil && (object["result"] != nil || object["error"] != nil) { return nil }
            return Self.error(id: id ?? NSNull(), code: -32600, "invalid request")
        }
        guard let id = id, id is String || id is NSNumber else {
            return id == nil ? nil : Self.error(id: NSNull(), code: -32600, "invalid id")
        }
        let params = object["params"] as? [String: Any] ?? [:]
        do {
            return ["jsonrpc": "2.0", "id": id, "result": try result(method, params)]
        } catch let error as RPCError {
            return Self.error(id: id, code: error.code, error.message)
        } catch {
            return Self.error(id: id, code: -32603, "\(error)")
        }
    }

    private func result(_ method: String, _ params: [String: Any]) throws -> [String: Any] {
        switch method {
        case "initialize":
            let requested = params["protocolVersion"] as? String ?? ""
            return [
                "protocolVersion": Self.protocolVersions.contains(requested) ? requested : Self.protocolVersions[0],
                "capabilities": ["tools": [String: Any](), "resources": [String: Any](), "prompts": [String: Any]()],
                "serverInfo": ["name": "whiteprint", "version": Self.version],
                "instructions": Self.instructions,
            ]
        case "ping":
            return [:]
        case "tools/list":
            return ["tools": MCPTool.all.map(\.json)]
        case "tools/call":
            return try callTool(params)
        case "resources/list":
            return ["resources": [[
                "uri": Self.dslURI, "name": "dsl", "description": "Drawing language reference", "mimeType": "text/plain",
            ]]]
        case "resources/templates/list":
            return ["resourceTemplates": [Any]()]
        case "resources/read":
            guard let uri = params["uri"] as? String else { throw RPCError(-32602, "missing uri") }
            guard uri == Self.dslURI else { throw RPCError(-32002, "resource not found: \(uri)") }
            return ["contents": [["uri": uri, "mimeType": "text/plain", "text": WhiteprintText.dslReference]]]
        case "prompts/list":
            let imports: [String: Any] = [
                "name": "imports", "description": "Import ids, comma-separated (default: all)", "required": false,
            ]
            let studyPlan: [String: Any] = [
                "name": "study_plan",
                "description": "Build a study plan from imported course material.",
                "arguments": [imports],
            ]
            return ["prompts": [studyPlan]]
        case "prompts/get":
            return try prompt(params)
        default:
            throw RPCError(-32601, "method not found: \(method)")
        }
    }

    private func callTool(_ params: [String: Any]) throws -> [String: Any] {
        guard let name = params["name"] as? String else { throw RPCError(-32602, "missing tool name") }
        guard let tool = MCPTool.named(name) else { throw RPCError(-32602, "unknown tool: \(name)") }
        let arguments = params["arguments"] ?? [String: Any]()
        guard let object = arguments as? [String: Any] else {
            return Self.toolResult("Invalid arguments: expected an object", isError: true)
        }
        let request: BridgeRequest
        do {
            request = try tool.makeRequest(object)
        } catch {
            return Self.toolResult("Invalid arguments: \(error)", isError: true)
        }
        do {
            switch try send(request) {
            case let .ok(text):
                return Self.toolResult(text, isError: false)
            case let .failure(text):
                return Self.toolResult(text, isError: true)
            }
        } catch {
            Self.log("\(name): \(error)")
            return Self.toolResult("\(error)", isError: true)
        }
    }

    private func prompt(_ params: [String: Any]) throws -> [String: Any] {
        guard let name = params["name"] as? String else { throw RPCError(-32602, "missing prompt name") }
        guard name == "study_plan" else { throw RPCError(-32602, "unknown prompt: \(name)") }
        var text = WhiteprintText.studyPlanPrompt
        let arguments = params["arguments"] as? [String: Any] ?? [:]
        if let imports = arguments["imports"] as? String, !imports.trimmingCharacters(in: .whitespaces).isEmpty {
            text += "\n\nImports: \(imports)"
        }
        return [
            "description": "Build a study plan",
            "messages": [["role": "user", "content": ["type": "text", "text": text]] as [String: Any]],
        ]
    }

    private static var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    }

    private static func toolResult(_ text: String, isError: Bool) -> [String: Any] {
        ["content": [["type": "text", "text": text]], "isError": isError]
    }

    private static func error(id: Any, code: Int, _ message: String) -> [String: Any] {
        ["jsonrpc": "2.0", "id": id, "error": ["code": code, "message": message] as [String: Any]]
    }

    private func encode(_ object: Any) -> String? {
        guard let data = try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys, .withoutEscapingSlashes]) else {
            Self.log("couldn't encode a response")
            return nil
        }
        return String(decoding: data, as: UTF8.self)
    }

    static func log(_ message: String) {
        FileHandle.standardError.write(Data("whiteprint-mcp: \(message)\n".utf8))
    }
}

private struct RPCError: Error {
    let code: Int
    let message: String

    init(_ code: Int, _ message: String) {
        self.code = code
        self.message = message
    }
}
