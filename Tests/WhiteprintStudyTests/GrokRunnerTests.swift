import Foundation
import XCTest
import WhiteprintCore
@testable import WhiteprintStudy

/// Answers the runner's requests from a script instead of the network.
final class GrokStub: URLProtocol {
    enum Reply {
        case json(Int, Any, headers: [String: String] = [:])
        case raw(Int, String)
        case failure(URLError.Code)
        /// Never answers, until the task is cancelled.
        case hang
    }

    private static let lock = NSLock()
    private static var replies: [Reply] = []
    private static var seen: [URLRequest] = []
    private static var bodies: [[String: Any]] = []
    private static var stops = 0

    static func reset(_ script: [Reply]) {
        lock.lock()
        defer { lock.unlock() }
        replies = script
        seen = []
        bodies = []
        stops = 0
    }

    static var requests: [URLRequest] { lock.lock(); defer { lock.unlock() }; return seen }
    static var requestBodies: [[String: Any]] { lock.lock(); defer { lock.unlock() }; return bodies }
    static var stopCount: Int { lock.lock(); defer { lock.unlock() }; return stops }

    static var session: URLSessionConfiguration {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [GrokStub.self]
        return configuration
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        Self.lock.lock()
        Self.seen.append(request)
        if let body = Self.body(of: request) {
            Self.bodies.append(body)
        }
        let reply = Self.replies.isEmpty ? Reply.raw(500, "script ran out") : Self.replies.removeFirst()
        Self.lock.unlock()
        switch reply {
        case let .json(status, object, headers):
            respond(status, (try? JSONSerialization.data(withJSONObject: object)) ?? Data(), headers: headers)
        case let .raw(status, text):
            respond(status, Data(text.utf8), headers: [:])
        case let .failure(code):
            client?.urlProtocol(self, didFailWithError: URLError(code))
        case .hang:
            break
        }
    }

    override func stopLoading() {
        Self.lock.lock()
        Self.stops += 1
        Self.lock.unlock()
    }

    private func respond(_ status: Int, _ data: Data, headers: [String: String]) {
        let response = HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: headers)!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: data)
        client?.urlProtocolDidFinishLoading(self)
    }

    private static func body(of request: URLRequest) -> [String: Any]? {
        var data = request.httpBody ?? Data()
        if let stream = request.httpBodyStream {
            stream.open()
            var buffer = [UInt8](repeating: 0, count: 4096)
            while stream.hasBytesAvailable {
                let n = stream.read(&buffer, maxLength: buffer.count)
                if n <= 0 { break }
                data.append(buffer, count: n)
            }
            stream.close()
        }
        return (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
    }
}

final class GrokRunnerTests: XCTestCase {
    private typealias Event = ClaudeCodeRunner.Event

    private let configuration = GrokRunner.Configuration(apiKey: "xai-secret", baseURL: URL(string: "https://grok.test/v1")!)
    private let tools = [RunnerTool(name: "list_imports", description: "List imports.", inputSchema: ["type": "object"])]
    private var events: [Event] = []
    private var executed: [(name: String, arguments: [String: Any])] = []

    override func setUp() {
        events = []
        executed = []
        GrokStub.reset([])
    }

    /// A runner whose tool calls succeed with `result(name)` (or fail when it starts with `!`).
    private func runner(_ result: @escaping (String) -> String = { "ok \($0)" }) -> GrokRunner {
        let runner = GrokRunner(configuration: configuration, tools: tools, store: nil, session: GrokStub.session) { [weak self] name, arguments, reply in
            XCTAssertTrue(Thread.isMainThread)
            self?.executed.append((name, arguments))
            let text = result(name)
            DispatchQueue.global().async { reply(text.hasPrefix("!") ? String(text.dropFirst()) : text, text.hasPrefix("!")) }
        }
        runner.backoff = 0.01
        return runner
    }

    private func start(_ runner: GrokRunner, imports: [String] = ["i1"]) throws {
        try runner.start(importIDs: imports) { [weak self] event in
            XCTAssertTrue(Thread.isMainThread)
            self?.events.append(event)
        }
    }

    private func waitForEnd(timeout: TimeInterval = 10) async throws -> [Event] {
        let deadline = Date().addingTimeInterval(timeout)
        while !(events.last?.isTerminal ?? false) {
            guard Date() < deadline else {
                XCTFail("run didn't end: \(events)")
                break
            }
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        // Anything delivered after the end would be a bug.
        try await Task.sleep(nanoseconds: 100_000_000)
        return events
    }

    private static func toolCalls(_ calls: [(id: String, name: String, arguments: String)], content: Any = NSNull()) -> GrokStub.Reply {
        let toolCalls = calls.map { call -> [String: Any] in
            ["id": call.id, "type": "function", "function": ["name": call.name, "arguments": call.arguments]]
        }
        let message: [String: Any] = ["role": "assistant", "content": content, "tool_calls": toolCalls]
        return .json(200, ["choices": [["index": 0, "message": message, "finish_reason": "tool_calls"]]])
    }

    private static func answer(_ text: String) -> GrokStub.Reply {
        .json(200, ["choices": [["index": 0, "message": ["role": "assistant", "content": text], "finish_reason": "stop"]]])
    }

    // MARK: tool loop

    func testRunsToolCallsUntilGrokAnswers() async throws {
        GrokStub.reset([
            Self.toolCalls([("call_1", "list_imports", "{}")]),
            Self.toolCalls([
                ("call_2", "read_chunk", #"{"import":"i1","chunk":1}"#),
                ("call_3", "save_points", #"{"import":"i1","chunk":1,"points":[]}"#),
            ], content: "Reading."),
            Self.answer("Done."),
        ])
        let runner = runner { $0 == "read_chunk" ? String(repeating: "chunk text ", count: 500) : "ok" }
        try start(runner, imports: ["i1", "i2"])
        XCTAssertTrue(runner.isRunning)

        let ended = try await waitForEnd()
        XCTAssertEqual(ended, [
            .status("Starting Grok…"), .status("Looking at your imports…"), .text("Reading."), .status("Reading chunk 1 · i1"),
            .status("Saving points…"), .text("Done."), .finished,
        ])
        XCTAssertFalse(runner.isRunning)
        XCTAssertEqual(executed.map(\.name), ["list_imports", "read_chunk", "save_points"])
        XCTAssertEqual(executed[1].arguments["chunk"] as? Int, 1)

        let requests = GrokStub.requests
        XCTAssertEqual(requests.count, 3)
        for request in requests {
            XCTAssertEqual(request.url?.absoluteString, "https://grok.test/v1/chat/completions")
            XCTAssertEqual(request.httpMethod, "POST")
            XCTAssertEqual(request.value(forHTTPHeaderField: "Authorization"), "Bearer xai-secret")
        }
        let bodies = GrokStub.requestBodies
        XCTAssertEqual(bodies.count, 3)
        XCTAssertEqual(bodies[0]["model"] as? String, "grok-4.7")
        let tool = try XCTUnwrap((bodies[0]["tools"] as? [[String: Any]])?.first)
        XCTAssertEqual(tool["type"] as? String, "function")
        let function = try XCTUnwrap(tool["function"] as? [String: Any])
        XCTAssertEqual(function["name"] as? String, "list_imports")
        XCTAssertEqual(function["description"] as? String, "List imports.")
        XCTAssertEqual(function["parameters"] as? [String: String], ["type": "object"])

        let first = try XCTUnwrap(bodies[0]["messages"] as? [[String: Any]])
        XCTAssertEqual(first.map { $0["role"] as? String }, ["system", "user"])
        XCTAssertEqual(first[0]["content"] as? String, WhiteprintText.studyPlanPrompt + "\n\nImports: i1, i2")

        let last = try XCTUnwrap(bodies[2]["messages"] as? [[String: Any]])
        XCTAssertEqual(last.map { $0["role"] as? String }, ["system", "user", "assistant", "tool", "assistant", "tool", "tool"])
        XCTAssertEqual(last[3]["tool_call_id"] as? String, "call_1")
        XCTAssertEqual(last[3]["content"] as? String, "ok")
        XCTAssertEqual(last[4]["content"] as? String, "Reading.")
        XCTAssertEqual((last[4]["tool_calls"] as? [[String: Any]])?.count, 2)
        XCTAssertEqual(last[5]["tool_call_id"] as? String, "call_2")
        XCTAssertEqual(last[5]["content"] as? String, "[chunk 1 of i1: read, points saved]")
        XCTAssertEqual(last[6]["tool_call_id"] as? String, "call_3")
    }

    func testChunkTextStaysUntilItsPointsAreSaved() async throws {
        GrokStub.reset([
            Self.toolCalls([("a", "read_chunk", #"{"import":"i1","chunk":1}"#)]),
            Self.toolCalls([("b", "read_chunk", #"{"import":"i1","chunk":2}"#)]),
            Self.toolCalls([("c", "save_points", #"{"import":"i1","chunk":"2","points":[]}"#)]),
            Self.answer(""),
        ])
        try start(runner { $0 == "read_chunk" ? "TEXT" : "ok" })
        let ended = try await waitForEnd()
        XCTAssertEqual(ended.last, .finished)
        let messages = try XCTUnwrap(GrokStub.requestBodies.last?["messages"] as? [[String: Any]])
        let results = messages.filter { $0["role"] as? String == "tool" }.compactMap { $0["content"] as? String }
        XCTAssertEqual(results, ["TEXT", "[chunk 2 of i1: read, points saved]", "ok"])
        XCTAssertFalse(events.contains(.text("")))
    }

    func testToolErrorsAndBadArgumentsGoBackToGrok() async throws {
        GrokStub.reset([
            Self.toolCalls([("a", "save_points", #"{"import":"i1","chunk":1}"#), ("b", "list_imports", "not json")]),
            Self.answer("Sorry."),
        ])
        try start(runner { _ in "!points: required" })
        let ended = try await waitForEnd()
        XCTAssertEqual(ended.last, .finished)
        XCTAssertEqual(executed.map(\.name), ["save_points"])
        let messages = try XCTUnwrap(GrokStub.requestBodies.last?["messages"] as? [[String: Any]])
        let results = messages.filter { $0["role"] as? String == "tool" }
        XCTAssertEqual(results.compactMap { $0["content"] as? String },
                       ["Error: points: required", "Error: Invalid arguments: expected a JSON object"])
        XCTAssertEqual(results.compactMap { $0["tool_call_id"] as? String }, ["a", "b"])
    }

    func testStopsAfterTooManyToolCalls() async throws {
        GrokStub.reset(Array(repeating: Self.toolCalls([("x", "list_imports", "{}")]), count: 10))
        let runner = runner()
        runner.maxToolCalls = 3
        try start(runner)
        let ended = try await waitForEnd()
        XCTAssertEqual(ended.last, .failed("Grok stopped after 3 tool calls without finishing the study plan."))
        XCTAssertEqual(executed.count, 3)
        XCTAssertEqual(GrokStub.requests.count, 4)
    }

    func testRefusesASecondRun() throws {
        GrokStub.reset([.hang])
        let runner = runner()
        try start(runner)
        XCTAssertThrowsError(try start(runner)) { XCTAssertEqual($0 as? StudyError, .alreadyRunning) }
        runner.cancel()
    }

    // MARK: errors

    func testRejectedKey() async throws {
        for status in [401, 403] {
            events = []
            GrokStub.reset([.json(status, ["error": "Incorrect API key provided: xa***et."])])
            try start(runner())
            let ended = try await waitForEnd()
            XCTAssertEqual(ended, [.status("Starting Grok…"), .failed("Grok rejected the API key. Check it in Settings.")])
            XCTAssertEqual(GrokStub.requests.count, 1)
        }
    }

    func testRetriesWhenBusyHonouringRetryAfter() async throws {
        GrokStub.reset([
            .json(429, ["error": "slow down"], headers: ["Retry-After": "0"]),
            .raw(503, "upstream"),
            Self.answer("Done."),
        ])
        try start(runner())
        let ended = try await waitForEnd()
        XCTAssertEqual(ended, [
            .status("Starting Grok…"), .status("Grok is busy, retrying…"), .status("Grok had a problem, retrying…"),
            .text("Done."), .finished,
        ])
        XCTAssertEqual(GrokStub.requests.count, 3)
    }

    func testGivesUpAfterThreeTries() async throws {
        GrokStub.reset([.raw(500, ""), .raw(502, ""), .json(500, ["error": ["message": "model overloaded\nplease wait"]])])
        try start(runner())
        let ended = try await waitForEnd()
        XCTAssertEqual(ended.last, .failed("Grok had a server problem (HTTP 500): model overloaded please wait"))
        XCTAssertEqual(GrokStub.requests.count, 3)

        events = []
        GrokStub.reset(Array(repeating: .json(429, [:], headers: ["Retry-After": "0"]), count: 3))
        try start(runner())
        let busy = try await waitForEnd()
        XCTAssertEqual(busy.last, .failed("Grok is busy or your rate limit is reached. Try again in a few minutes."))
        XCTAssertEqual(GrokStub.requests.count, 3)
    }

    func testOtherFailures() async throws {
        let cases: [(GrokStub.Reply, Event)] = [
            (.failure(.notConnectedToInternet), .failed("Couldn't reach Grok: \(URLError(.notConnectedToInternet).localizedDescription)")),
            (.raw(200, "<html>"), .failed("Grok sent an invalid response.")),
            (.json(200, ["choices": []]), .failed("Grok sent an invalid response.")),
            (.json(400, ["error": "Model not found: grok-9"]), .failed("Grok returned HTTP 400: Model not found: grok-9")),
        ]
        for (reply, expected) in cases {
            events = []
            GrokStub.reset([reply])
            try start(runner())
            let ended = try await waitForEnd()
            XCTAssertEqual(ended.last, expected)
            XCTAssertEqual(GrokStub.requests.count, 1)
        }
    }

    // MARK: cancellation

    func testCancelStopsTheRequestInFlight() async throws {
        GrokStub.reset([.hang])
        let runner = runner()
        try start(runner)
        try await Task.sleep(nanoseconds: 100_000_000)
        runner.cancel()
        let ended = try await waitForEnd()
        XCTAssertEqual(ended, [.status("Starting Grok…"), .failed("Cancelled")])
        XCTAssertFalse(runner.isRunning)
        XCTAssertEqual(GrokStub.stopCount, 1)
    }

    func testCancelDuringAToolCallSendsNothingMore() async throws {
        GrokStub.reset([Self.toolCalls([("a", "list_imports", "{}"), ("b", "list_imports", "{}")]), Self.answer("Done.")])
        var pending: ((String, Bool) -> Void)?
        let runner = GrokRunner(configuration: configuration, tools: tools, store: nil, session: GrokStub.session) { _, _, reply in
            pending = reply
        }
        try start(runner)
        while pending == nil {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        runner.cancel()
        try await Task.sleep(nanoseconds: 50_000_000)
        pending?("ok", false)
        let ended = try await waitForEnd()
        XCTAssertEqual(ended.last, .failed("Cancelled"))
        XCTAssertEqual(GrokStub.requests.count, 1)
        XCTAssertFalse(runner.isRunning)
    }

    // MARK: testConnection

    private func connect(_ script: [GrokStub.Reply]) async throws -> Result<String, Error> {
        GrokStub.reset(script)
        var result: Result<String, Error>?
        GrokRunner.testConnection(configuration, session: GrokStub.session) {
            XCTAssertTrue(Thread.isMainThread)
            result = $0
        }
        while result == nil {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        return result!
    }

    func testConnectionLooksTheModelUp() async throws {
        let result = try await connect([.json(200, ["id": "grok-4.7", "object": "model", "owned_by": "xai"])])
        XCTAssertEqual(try result.get(), "grok-4.7")
        let request = try XCTUnwrap(GrokStub.requests.first)
        XCTAssertEqual(request.url?.absoluteString, "https://grok.test/v1/models/grok-4.7")
        XCTAssertEqual(request.httpMethod, "GET")
        XCTAssertEqual(request.value(forHTTPHeaderField: "Authorization"), "Bearer xai-secret")
    }

    func testConnectionFailures() async throws {
        let cases: [(GrokStub.Reply, String)] = [
            (.json(401, ["error": "bad key"]), "Grok rejected the API key. Check it in Settings."),
            (.json(404, [:]), "Grok doesn't know the model \u{201C}grok-4.7\u{201D}."),
            (.failure(.timedOut), "Couldn't reach Grok: \(URLError(.timedOut).localizedDescription)"),
        ]
        for (reply, message) in cases {
            switch try await connect([reply]) {
            case .success(let id): XCTFail("connected to \(id)")
            case .failure(let error): XCTAssertEqual("\(error)", message)
            }
        }
    }
}
