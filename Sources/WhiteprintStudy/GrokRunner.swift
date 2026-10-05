import Foundation

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
///
/// Talks to the OpenAI-compatible `POST {baseURL}/chat/completions`. The API is
/// stateless, so every turn resends the history; to keep it small, a chunk's
/// `read_chunk` text is replaced by a placeholder once its points are saved.
public final class GrokRunner {
    public struct Configuration: Equatable {
        public var apiKey: String
        public var model: String
        public var baseURL: URL

        /// xAI's recommended general model (docs.x.ai, October 2026).
        public static let defaultModel = "grok-4.7"
        public static let defaultBaseURL = URL(string: "https://api.x.ai/v1")!

        public init(apiKey: String, model: String = defaultModel, baseURL: URL = defaultBaseURL) {
            self.apiKey = apiKey
            self.model = model
            self.baseURL = baseURL
        }
    }

    /// Runs one tool call; call `reply(text, isError)` exactly once, from any queue.
    public typealias ToolExecutor = (_ name: String, _ arguments: [String: Any], _ reply: @escaping (String, Bool) -> Void) -> Void

    /// A run stops with an error after this many tool calls.
    var maxToolCalls = 250
    /// Requests are tried this often when Grok is busy (429) or failing (5xx).
    static let maxAttempts = 3

    private let configuration: Configuration
    private let tools: [[String: Any]]
    private let execute: ToolExecutor
    private let importInfo: (String) -> StudyImport?
    private let session: URLSession
    private let queue = DispatchQueue(label: "Whiteprint.GrokRunner")
    private var run: Run?
    /// Seconds before retry `n` (1-based) when Grok sends no Retry-After: 1, 2, 4… times this.
    var backoff: TimeInterval = 1

    /// `store` (optional) names files and chunk counts in progress messages.
    /// `execute` is called on the main queue.
    public convenience init(configuration: Configuration, tools: [RunnerTool], store: StudyStore? = nil,
                            execute: @escaping ToolExecutor) {
        self.init(configuration: configuration, tools: tools, store: store, session: .ephemeral, execute: execute)
    }

    init(configuration: Configuration, tools: [RunnerTool], store: StudyStore?, session: URLSessionConfiguration,
         execute: @escaping ToolExecutor) {
        self.configuration = configuration
        self.tools = tools.map { tool in
            let function: [String: Any] = ["name": tool.name, "description": tool.description, "parameters": tool.inputSchema]
            return ["type": "function", "function": function]
        }
        self.execute = execute
        importInfo = { [weak store] id in store?.imports.first { $0.id == id } }
        self.session = URLSession(configuration: session)
    }

    deinit {
        session.invalidateAndCancel()
    }

    public var isRunning: Bool {
        queue.sync { run != nil }
    }

    /// Starts the tool loop for the given imports (all when empty). Events are
    /// delivered on the main queue, ending with exactly one `.finished` or
    /// `.failed`. Throws `StudyError.alreadyRunning` while a run is in progress.
    public func start(importIDs: [String], onEvent: @escaping (ClaudeCodeRunner.Event) -> Void) throws {
        try queue.sync {
            guard run == nil else { throw StudyError.alreadyRunning }
            let current = Run(onEvent: onEvent)
            run = current
            current.messages = [
                ["role": "system", "content": ClaudeCodeRunner.prompt(importIDs: importIDs)],
                ["role": "user", "content": "Make the study plan."],
            ]
            current.deliver(.status("Starting Grok…"))
            send(current)
        }
    }

    /// Stops the run, including any request in flight. The run then ends with `.failed("Cancelled")`.
    public func cancel() {
        queue.async { [weak self] in
            guard let self, let current = self.run else { return }
            current.task?.cancel()
            self.end(current, .failed("Cancelled"))
        }
    }

    /// Checks the key and model by looking the model up (`GET {baseURL}/models/{model}`),
    /// which uses no tokens. Succeeds with the model id. Completion on the main queue.
    public static func testConnection(_ configuration: Configuration, completion: @escaping (Result<String, Error>) -> Void) {
        testConnection(configuration, session: .ephemeral, completion: completion)
    }

    static func testConnection(_ configuration: Configuration, session sessionConfiguration: URLSessionConfiguration,
                               completion: @escaping (Result<String, Error>) -> Void) {
        let session = URLSession(configuration: sessionConfiguration)
        var request = URLRequest(url: configuration.baseURL.appendingPathComponent("models").appendingPathComponent(configuration.model))
        request.setValue("Bearer \(configuration.apiKey)", forHTTPHeaderField: "Authorization")
        request.timeoutInterval = 20
        session.dataTask(with: request) { data, response, error in
            session.finishTasksAndInvalidate()
            let result: Result<String, Error>
            switch Reply(data: data, response: response, error: error) {
            case let .ok(object):
                result = .success(object["id"] as? String ?? configuration.model)
            case .status(404, _, _):
                result = .failure(GrokError("Grok doesn't know the model \u{201C}\(configuration.model)\u{201D}."))
            case let failure:
                result = .failure(GrokError(failure.message ?? "Grok sent an invalid response."))
            }
            DispatchQueue.main.async { completion(result) }
        }.resume()
    }
}

/// A Grok failure, described in one line for the user.
public struct GrokError: Error, Equatable, CustomStringConvertible {
    public let description: String

    init(_ description: String) {
        self.description = description
    }
}

// MARK: - Running

private extension GrokRunner {
    /// One run's conversation and state. Touched only on `queue`.
    final class Run {
        let onEvent: (ClaudeCodeRunner.Event) -> Void
        var messages: [[String: Any]] = []
        var toolCalls = 0
        /// Index in `messages` of each chunk's `read_chunk` result, by `import#chunk`.
        var chunkReads: [String: Int] = [:]
        var task: URLSessionDataTask?
        var ended = false

        init(onEvent: @escaping (ClaudeCodeRunner.Event) -> Void) {
            self.onEvent = onEvent
        }

        func deliver(_ event: ClaudeCodeRunner.Event) {
            guard !ended else { return }
            ended = event.isTerminal
            let onEvent = self.onEvent
            DispatchQueue.main.async { onEvent(event) }
        }
    }

    func end(_ current: Run, _ event: ClaudeCodeRunner.Event) {
        current.deliver(event)
        if run === current {
            run = nil
        }
    }

    func send(_ current: Run, attempt: Int = 1) {
        guard !current.ended else { return }
        let body: [String: Any] = [
            "model": configuration.model, "messages": current.messages, "tools": tools, "tool_choice": "auto",
        ]
        var request = URLRequest(url: configuration.baseURL.appendingPathComponent("chat/completions"))
        request.httpMethod = "POST"
        request.setValue("Bearer \(configuration.apiKey)", forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.timeoutInterval = 600
        do {
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        } catch {
            return end(current, .failed("Couldn't encode the request for Grok."))
        }
        // Strong: the runner stays alive while a request is in flight.
        let task = session.dataTask(with: request) { data, response, error in
            self.queue.async { self.received(Reply(data: data, response: response, error: error), for: current, attempt: attempt) }
        }
        current.task = task
        task.resume()
    }

    func received(_ reply: Reply, for current: Run, attempt: Int) {
        guard !current.ended else { return }
        current.task = nil
        switch reply {
        case let .ok(object):
            guard let message = ((object["choices"] as? [[String: Any]])?.first)?["message"] as? [String: Any] else {
                return end(current, .failed("Grok sent an invalid response."))
            }
            answered(message, for: current)
        case let .status(code, retryAfter, _) where (code == 429 || code >= 500) && attempt < Self.maxAttempts:
            let delay = retryAfter.map { min($0, 60) } ?? backoff * pow(2, Double(attempt - 1))
            current.deliver(.status(code == 429 ? "Grok is busy, retrying…" : "Grok had a problem, retrying…"))
            queue.asyncAfter(deadline: .now() + delay) { self.send(current, attempt: attempt + 1) }
        default:
            end(current, .failed(reply.message ?? "Grok sent an invalid response."))
        }
    }

    func answered(_ message: [String: Any], for current: Run) {
        let text = (message["content"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
        if !text.isEmpty {
            current.deliver(.text(text))
        }
        let calls = message["tool_calls"] as? [[String: Any]] ?? []
        guard !calls.isEmpty else { return end(current, .finished) }
        var assistant: [String: Any] = ["role": "assistant", "tool_calls": calls]
        assistant["content"] = text.isEmpty ? NSNull() : text
        current.messages.append(assistant)
        runTools(calls[...], for: current)
    }

    /// Runs `calls` one after another, then asks Grok to continue.
    func runTools(_ calls: ArraySlice<[String: Any]>, for current: Run) {
        guard !current.ended else { return }
        guard let call = calls.first else { return send(current) }
        current.toolCalls += 1
        guard current.toolCalls <= maxToolCalls else {
            return end(current, .failed("Grok stopped after \(maxToolCalls) tool calls without finishing the study plan."))
        }
        let id = call["id"] as? String ?? ""
        let function = call["function"] as? [String: Any] ?? [:]
        let name = function["name"] as? String ?? ""
        guard let arguments = Self.arguments(function["arguments"]) else {
            record("Invalid arguments: expected a JSON object", isError: true, id: id, for: current)
            return runTools(calls.dropFirst(), for: current)
        }
        current.deliver(.status(ToolStatus.text(tool: name, input: arguments, importInfo: importInfo)))
        let execute = self.execute
        DispatchQueue.main.async {
            var replied = false
            execute(name, arguments) { text, isError in
                self.queue.async {
                    guard !replied else { return }
                    replied = true
                    guard !current.ended else { return }
                    self.record(text, isError: isError, id: id, for: current)
                    if !isError {
                        self.trimHistory(after: name, arguments, for: current)
                    }
                    self.runTools(calls.dropFirst(), for: current)
                }
            }
        }
    }

    func record(_ text: String, isError: Bool, id: String, for current: Run) {
        current.messages.append(["role": "tool", "tool_call_id": id, "content": isError ? "Error: \(text)" : text])
    }

    /// Remembers where a chunk was read, and once its points are saved replaces
    /// that (long) result with a placeholder: Grok never needs it again.
    func trimHistory(after tool: String, _ arguments: [String: Any], for current: Run) {
        guard let importID = arguments["import"] as? String, let chunk = Self.int(arguments["chunk"]) else { return }
        let key = "\(importID)#\(chunk)"
        switch tool {
        case "read_chunk":
            current.chunkReads[key] = current.messages.count - 1
        case "save_points":
            guard let index = current.chunkReads.removeValue(forKey: key) else { return }
            current.messages[index]["content"] = "[chunk \(chunk) of \(importID): read, points saved]"
        default:
            break
        }
    }

    /// A call's arguments: a JSON string per the API, tolerating an object or nothing.
    static func arguments(_ value: Any?) -> [String: Any]? {
        if let object = value as? [String: Any] { return object }
        let text = (value as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
        return (try? JSONSerialization.jsonObject(with: Data((text.isEmpty ? "{}" : text).utf8))) as? [String: Any]
    }

    static func int(_ value: Any?) -> Int? {
        if let number = value as? NSNumber { return Int(exactly: number.doubleValue) }
        return (value as? String).flatMap { Int($0) }
    }
}

// MARK: - Replies

/// An HTTP exchange with Grok, classified.
private enum Reply {
    case ok([String: Any])
    /// Any other status, with Retry-After (seconds) and Grok's error message.
    case status(Int, retryAfter: TimeInterval?, detail: String?)
    case network(String)
    case invalid

    init(data: Data?, response: URLResponse?, error: Error?) {
        if let error {
            self = .network(error.localizedDescription)
            return
        }
        guard let http = response as? HTTPURLResponse else {
            self = .invalid
            return
        }
        let object = data.flatMap { try? JSONSerialization.jsonObject(with: $0) } as? [String: Any]
        guard (200..<300).contains(http.statusCode) else {
            let retryAfter = (http.value(forHTTPHeaderField: "Retry-After")?.trimmingCharacters(in: .whitespaces))
                .flatMap(TimeInterval.init).map { max($0, 0) }
            self = .status(http.statusCode, retryAfter: retryAfter, detail: Self.detail(object))
            return
        }
        self = object.map(Reply.ok) ?? .invalid
    }

    /// `{"error": "…"}`, `{"error": {"message": "…"}}` or `{"message": "…"}`, cut to one line.
    private static func detail(_ object: [String: Any]?) -> String? {
        let error = object?["error"]
        let text = error as? String ?? (error as? [String: Any])?["message"] as? String ?? object?["message"] as? String
        guard let line = text.map(oneLine), !line.isEmpty else { return nil }
        return line.count > 200 ? String(line.prefix(200)) + "…" : line
    }

    /// The failure in one line for the user; nil for `.ok`.
    var message: String? {
        switch self {
        case .ok:
            return nil
        case .status(401, _, _), .status(403, _, _):
            return "Grok rejected the API key. Check it in Settings."
        case .status(429, _, _):
            return "Grok is busy or your rate limit is reached. Try again in a few minutes."
        case let .status(code, _, detail) where code >= 500:
            return "Grok had a server problem (HTTP \(code))" + (detail.map { ": \($0)" } ?? ". Try again later.")
        case let .status(code, _, detail):
            return "Grok returned HTTP \(code)" + (detail.map { ": \($0)" } ?? ".")
        case let .network(reason):
            return "Couldn't reach Grok: \(reason)"
        case .invalid:
            return "Grok sent an invalid response."
        }
    }
}
