import Foundation

// CONTRACT (owner: backend agent). Public signatures are fixed; bodies are stubs.

/// A tool offered to an API-based agent: name, description, JSON Schema.
public struct RunnerTool {
    public var name: String
    public var description: String
    public var inputSchema: [String: Any]

    public init(name: String, description: String, inputSchema: [String: Any]) {
        self.name = name
        self.description = description
        self.inputSchema = inputSchema
    }
}

/// Builds the study plan with xAI's Grok API instead of Claude Code, for people
/// who'd rather use an API key. Runs the same tool loop as Claude: the app
/// executes each tool call through the same handler the MCP helper uses.
public final class GrokRunner {
    public struct Configuration: Equatable {
        public var apiKey: String
        public var model: String
        public var baseURL: URL

        public static let defaultModel = "grok-4"
        public static let defaultBaseURL = URL(string: "https://api.x.ai/v1")!

        public init(apiKey: String, model: String = defaultModel, baseURL: URL = defaultBaseURL) {
            self.apiKey = apiKey
            self.model = model
            self.baseURL = baseURL
        }
    }

    /// Runs one tool call; call `reply(text, isError)` exactly once, from any queue.
    public typealias ToolExecutor = (_ name: String, _ arguments: [String: Any], _ reply: @escaping (String, Bool) -> Void) -> Void

    public init(configuration: Configuration, tools: [RunnerTool], execute: @escaping ToolExecutor) {}

    public var isRunning: Bool { false }

    /// Same events as `ClaudeCodeRunner`, delivered on the main queue.
    public func start(importIDs: [String], onEvent: @escaping (ClaudeCodeRunner.Event) -> Void) throws {}

    public func cancel() {}

    /// Checks the key and model with a tiny request. Completion on the main queue.
    public static func testConnection(_ configuration: Configuration, completion: @escaping (Result<String, Error>) -> Void) {
        completion(.failure(StudyError.unknownImport("not implemented")))
    }
}
