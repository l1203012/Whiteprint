import Darwin
import Foundation
import XCTest
import WhiteprintCore
@testable import WhiteprintStudy

final class ClaudeCodeRunnerTests: XCTestCase {
    private typealias Event = ClaudeCodeRunner.Event

    private var root: URL!
    private let helper = URL(fileURLWithPath: "/Applications/Whiteprint.app/Contents/MacOS/whiteprint-mcp")

    override func setUp() {
        root = FileManager.default.temporaryDirectory.appendingPathComponent("RunnerTests-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: root)
    }

    /// Collects events delivered on the main queue.
    private final class Recorder {
        private let lock = NSLock()
        private var list: [Event] = []

        var events: [Event] {
            lock.lock()
            defer { lock.unlock() }
            return list
        }

        func append(_ event: Event) {
            XCTAssertTrue(Thread.isMainThread)
            lock.lock()
            list.append(event)
            lock.unlock()
        }
    }

    /// A stand-in for `claude` that records how it was started, then runs `body`.
    private func fakeClaude(_ body: String) throws -> URL {
        let url = root.appendingPathComponent("claude")
        let script = """
        #!/bin/sh
        out='\(root.path)'
        for a in "$@"; do printf '%s\\0' "$a"; done > "$out/args"
        pwd -P > "$out/cwd"
        cp mcp.json "$out/mcp.json"
        printf '%s' "$PATH" > "$out/path"
        \(body)
        """
        try script.write(to: url, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: url.path)
        return url
    }

    private func samples(_ lines: [String]) throws -> String {
        let url = root.appendingPathComponent("samples.jsonl")
        try (lines.joined(separator: "\n") + "\n").write(to: url, atomically: true, encoding: .utf8)
        return url.path
    }

    private func waitForEnd(_ recorder: Recorder, timeout: TimeInterval = 15) async throws -> [Event] {
        let deadline = Date().addingTimeInterval(timeout)
        while !(recorder.events.last?.isTerminal ?? false) {
            guard Date() < deadline else {
                XCTFail("run didn't end: \(recorder.events)")
                break
            }
            try await Task.sleep(nanoseconds: 20_000_000)
        }
        // Anything delivered after the end would be a bug.
        try await Task.sleep(nanoseconds: 100_000_000)
        return recorder.events
    }

    private func isAlive(_ pid: pid_t) -> Bool {
        kill(pid, 0) == 0
    }

    func testSuccessfulRun() async throws {
        let claude = try fakeClaude("cat '\(try samples(StreamSamples.run))'")
        let store = try StudyStore(directory: root.appendingPathComponent("Study"))
        let runner = ClaudeCodeRunner(claudeURL: claude, store: store)
        let recorder = Recorder()
        try runner.start(importIDs: ["i1", "i2"], helperURL: helper, onEvent: recorder.append)
        let events = try await waitForEnd(recorder)

        XCTAssertEqual(events.first, .status("Starting Claude…"))
        XCTAssertTrue(events.contains(.status("Reading chunk 3 · i1")))
        XCTAssertEqual(events.last, .finished)
        XCTAssertEqual(events.filter(\.isTerminal).count, 1)
        XCTAssertFalse(runner.isRunning)

        let args = try String(contentsOf: root.appendingPathComponent("args")).split(separator: "\0").map(String.init)
        let configIndex = try XCTUnwrap(args.firstIndex(of: "--mcp-config")) + 1
        let configPath = args[configIndex]
        XCTAssertEqual(args, ClaudeCodeRunner.arguments(
            prompt: WhiteprintText.studyPlanPrompt + "\n\nImports: i1, i2",
            configPath: configPath
        ))
        let cwd = try String(contentsOf: root.appendingPathComponent("cwd")).trimmingCharacters(in: .newlines)
        // The config sits in the working directory (which may be reported via /private).
        XCTAssertTrue(cwd.hasSuffix(URL(fileURLWithPath: configPath).deletingLastPathComponent().path), cwd)

        let config = try JSONSerialization.jsonObject(with: Data(contentsOf: root.appendingPathComponent("mcp.json"))) as? [String: Any]
        let server = (config?["mcpServers"] as? [String: Any])?["whiteprint"] as? [String: Any]
        XCTAssertEqual(server?["command"] as? String, helper.path)
        XCTAssertEqual((server?["args"] as? [Any])?.count, 0)

        let path = try String(contentsOf: root.appendingPathComponent("path")).split(separator: ":").map(String.init)
        XCTAssertTrue(path.contains(root.path))
        XCTAssertTrue(path.contains("/usr/bin"))
        XCTAssertFalse(FileManager.default.fileExists(atPath: configPath), "temporary MCP config left behind")
    }

    func testNonZeroExitReportsStderr() async throws {
        let claude = try fakeClaude("echo 'starting' >&2\necho 'Error: not logged in' >&2\nexit 3")
        let recorder = Recorder()
        try ClaudeCodeRunner(claudeURL: claude).start(importIDs: [], helperURL: helper, onEvent: recorder.append)
        let events = try await waitForEnd(recorder)
        XCTAssertEqual(events, [.failed("Claude Code exited with status 3: Error: not logged in")])
        let args = try String(contentsOf: root.appendingPathComponent("args")).split(separator: "\0")
        XCTAssertTrue(args[1].hasSuffix("Imports: all"))
    }

    func testErrorResultFails() async throws {
        let claude = try fakeClaude("cat '\(try samples([StreamSamples.initLine, StreamSamples.notLoggedIn]))'\nexit 1")
        let recorder = Recorder()
        try ClaudeCodeRunner(claudeURL: claude).start(importIDs: ["i1"], helperURL: helper, onEvent: recorder.append)
        let events = try await waitForEnd(recorder)
        XCTAssertEqual(events, [.status("Starting Claude…"), .failed("Invalid API key · Please run /login")])
    }

    func testExitWithoutResultFails() async throws {
        let claude = try fakeClaude("cat '\(try samples(Array(StreamSamples.run.prefix(3))))'")
        let recorder = Recorder()
        try ClaudeCodeRunner(claudeURL: claude).start(importIDs: ["i1"], helperURL: helper, onEvent: recorder.append)
        let events = try await waitForEnd(recorder)
        XCTAssertEqual(events.last, .failed("Claude Code stopped without finishing the study plan."))
    }

    func testMissingBinaryFails() async throws {
        let missing = root.appendingPathComponent("nope/claude")
        let runner = ClaudeCodeRunner(claudeURL: missing)
        let recorder = Recorder()
        try runner.start(importIDs: ["i1"], helperURL: helper, onEvent: recorder.append)
        let events = try await waitForEnd(recorder)
        XCTAssertEqual(events, [.failed("Claude Code wasn't found at \(missing.path).")])
        XCTAssertFalse(runner.isRunning)
    }

    func testCancelStopsTheProcessTree() async throws {
        let claude = try fakeClaude("""
        cat '\(try samples([StreamSamples.initLine]))'
        sleep 60 &
        echo $! > "$out/child"
        wait
        """)
        let runner = ClaudeCodeRunner(claudeURL: claude)
        let recorder = Recorder()
        try runner.start(importIDs: ["i1"], helperURL: helper, onEvent: recorder.append)

        let childFile = root.appendingPathComponent("child")
        let deadline = Date().addingTimeInterval(10)
        while recorder.events.isEmpty || !FileManager.default.fileExists(atPath: childFile.path) {
            guard Date() < deadline else { return XCTFail("fake claude didn't start") }
            try await Task.sleep(nanoseconds: 20_000_000)
        }
        try await Task.sleep(nanoseconds: 100_000_000)
        let child = try XCTUnwrap(pid_t(String(contentsOf: childFile).trimmingCharacters(in: .whitespacesAndNewlines)))
        XCTAssertTrue(runner.isRunning)
        XCTAssertTrue(isAlive(child))
        XCTAssertThrowsError(try runner.start(importIDs: [], helperURL: helper) { _ in }) { error in
            XCTAssertEqual(error as? StudyError, .alreadyRunning)
        }

        runner.cancel()
        let events = try await waitForEnd(recorder)
        XCTAssertEqual(events, [.status("Starting Claude…"), .failed("Cancelled")])
        XCTAssertFalse(runner.isRunning)
        try await Task.sleep(nanoseconds: 200_000_000)
        XCTAssertFalse(isAlive(child), "child process survived cancel")
    }

    func testHelperFailureStopsTheRun() async throws {
        let claude = try fakeClaude("cat '\(try samples([StreamSamples.helperFailed]))'\nsleep 60")
        let recorder = Recorder()
        let started = Date()
        try ClaudeCodeRunner(claudeURL: claude).start(importIDs: ["i1"], helperURL: helper, onEvent: recorder.append)
        let events = try await waitForEnd(recorder)
        XCTAssertEqual(events, [.failed("Claude Code couldn't start Whiteprint's tools. Try reinstalling Whiteprint.")])
        XCTAssertLessThan(Date().timeIntervalSince(started), 10)
    }

    func testLeftoverChildDoesNotHoldUpTheEnd() async throws {
        let claude = try fakeClaude("""
        cat '\(try samples(StreamSamples.run))'
        sleep 60 &
        echo $! > "$out/child"
        """)
        let runner = ClaudeCodeRunner(claudeURL: claude)
        let recorder = Recorder()
        try runner.start(importIDs: ["i1"], helperURL: helper, onEvent: recorder.append)
        let events = try await waitForEnd(recorder)
        XCTAssertEqual(events.last, .finished)
        let deadline = Date().addingTimeInterval(5)
        while runner.isRunning && Date() < deadline {
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        XCTAssertFalse(runner.isRunning)
        try await Task.sleep(nanoseconds: 200_000_000)
        let child = try XCTUnwrap(pid_t(String(contentsOf: root.appendingPathComponent("child")).trimmingCharacters(in: .whitespacesAndNewlines)))
        XCTAssertFalse(isAlive(child), "leftover child kept running")
    }

    func testArguments() {
        XCTAssertEqual(ClaudeCodeRunner.arguments(prompt: "P", configPath: "/tmp/x/mcp.json"), [
            "-p", "P",
            "--output-format", "stream-json",
            "--verbose",
            "--strict-mcp-config",
            "--mcp-config", "/tmp/x/mcp.json",
            "--allowedTools", "mcp__whiteprint__*,ReadMcpResourceTool",
            "--permission-mode", "dontAsk",
            "--no-session-persistence",
        ])
    }

    func testLocateClaudeFindsAnExecutable() {
        if let url = ClaudeCodeRunner.locateClaude() {
            XCTAssertTrue(FileManager.default.isExecutableFile(atPath: url.path))
            XCTAssertEqual(url.lastPathComponent, "claude")
        }
    }

    func testPromptIsCompact() {
        let prompt = WhiteprintText.studyPlanPrompt
        XCTAssertLessThan(prompt.count, 2000)
        for tool in ["list_imports", "read_chunk", "save_points", "get_points", "build_study_plan", "whiteprint://dsl"] {
            XCTAssertTrue(prompt.contains(tool), tool)
        }
    }
}
