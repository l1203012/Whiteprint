import Darwin
import Foundation
import WhiteprintCore

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

        var isTerminal: Bool {
            switch self {
            case .finished, .failed: return true
            case .status, .text: return false
            }
        }
    }

    /// Looks for `claude` on the login shell's PATH and in the usual install
    /// locations (`~/.local/bin`, `/opt/homebrew/bin`, `/usr/local/bin`).
    /// Runs a login shell, so call it off the main thread.
    public static func locateClaude() -> URL? {
        let fileManager = FileManager.default
        if let path = LoginShell.shared.claudePath, fileManager.isExecutableFile(atPath: path) {
            return URL(fileURLWithPath: path)
        }
        let home = fileManager.homeDirectoryForCurrentUser.path
        let candidates = [
            "\(home)/.local/bin/claude", "/opt/homebrew/bin/claude", "/usr/local/bin/claude", "\(home)/.claude/local/claude",
        ]
        return candidates.first(where: fileManager.isExecutableFile(atPath:)).map(URL.init(fileURLWithPath:))
    }

    public let claudeURL: URL
    private let importInfo: (String) -> StudyImport?
    private let queue = DispatchQueue(label: "Whiteprint.ClaudeCodeRunner")
    private var run: Run?

    /// `store` (optional) names files and chunk counts in progress messages.
    public init(claudeURL: URL, store: StudyStore? = nil) {
        self.claudeURL = claudeURL
        importInfo = { [weak store] id in store?.imports.first { $0.id == id } }
    }

    public var isRunning: Bool {
        queue.sync { run != nil }
    }

    /// Starts `claude -p <prompt> --output-format stream-json --verbose
    /// --strict-mcp-config --mcp-config <file> --allowedTools mcp__whiteprint__*,…
    /// --permission-mode dontAsk --no-session-persistence` for the given imports
    /// (see `arguments`), in a temporary directory holding the MCP config. Events are
    /// delivered on the main queue, ending with exactly one `.finished` or `.failed`.
    /// Throws `StudyError.alreadyRunning` while a run is in progress. Looks up the
    /// login shell's PATH on first use, so it may block briefly.
    public func start(importIDs: [String], helperURL: URL, onEvent: @escaping (Event) -> Void) throws {
        let environment = Self.environment(claudeURL: claudeURL)
        try queue.sync {
            guard run == nil else { throw StudyError.alreadyRunning }
            let folder = FileManager.default.temporaryDirectory
                .appendingPathComponent("Whiteprint-\(UUID().uuidString)", isDirectory: true)
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            let config = folder.appendingPathComponent("mcp.json")
            do {
                try Self.mcpConfig(helperURL: helperURL).write(to: config, options: .atomic)
            } catch {
                try? FileManager.default.removeItem(at: folder)
                throw error
            }

            let process = Process()
            process.executableURL = claudeURL
            process.arguments = Self.arguments(prompt: Self.prompt(importIDs: importIDs), configPath: config.path)
            process.environment = environment
            process.currentDirectoryURL = folder
            process.standardInput = FileHandle.nullDevice
            let stdout = Pipe(), stderr = Pipe()
            process.standardOutput = stdout
            process.standardError = stderr

            let current = Run(process: process, folder: folder, parser: ClaudeStreamParser(importInfo: importInfo), onEvent: onEvent)
            run = current
            guard FileManager.default.isExecutableFile(atPath: claudeURL.path) else {
                current.deliver(.failed("Claude Code wasn't found at \(claudeURL.path)."))
                finish(current)
                return
            }

            // Once Claude Code exits, its output is read to the end, unless something
            // it left behind keeps the pipes open: then it's not waited for long.
            let readers = DispatchGroup()
            readers.enter()
            readers.enter()
            process.terminationHandler = { _ in
                DispatchQueue.global().async {
                    _ = readers.wait(timeout: .now() + 2)
                    self.queue.async { self.exited(current) }
                }
            }
            do {
                try process.run()
            } catch {
                current.deliver(.failed("Claude Code couldn't be started: \(error.localizedDescription)"))
                finish(current)
                return
            }
            let pid = process.processIdentifier
            current.ownGroup = getpgid(pid) == pid && pid != getpgrp()
            // Strong references: the runner stays alive until its process has exited.
            read(stdout.fileHandleForReading, done: readers) { data in self.output(data, of: current) }
            read(stderr.fileHandleForReading, done: readers) { data in current.appendError(data) }
        }
    }

    /// Stops the run and everything it started (Claude Code and the MCP helper).
    /// The run then ends with `.failed("Cancelled")`.
    public func cancel() {
        queue.async { [weak self] in
            guard let current = self?.run else { return }
            Self.stop(current)
        }
    }
}

// MARK: - Running

private extension ClaudeCodeRunner {
    /// One `claude -p` process and what it has reported so far. Touched only on `queue`.
    final class Run {
        let process: Process
        let folder: URL
        var parser: ClaudeStreamParser
        let onEvent: (Event) -> Void
        var cancelled = false
        var ended = false
        /// `Process` normally starts the child as leader of its own process group,
        /// so signalling the group reaches everything it spawned.
        var ownGroup = false
        /// The end of stderr, for the error message when Claude Code exits without a result.
        var errorTail = Data()

        init(process: Process, folder: URL, parser: ClaudeStreamParser, onEvent: @escaping (Event) -> Void) {
            self.process = process
            self.folder = folder
            self.parser = parser
            self.onEvent = onEvent
        }

        func deliver(_ event: Event) {
            guard !ended else { return }
            ended = event.isTerminal
            let onEvent = self.onEvent
            DispatchQueue.main.async { onEvent(event) }
        }

        func appendError(_ data: Data) {
            errorTail.append(data)
            if errorTail.count > 4096 {
                errorTail = errorTail.suffix(2048)
            }
        }
    }

    /// Reads `handle` until EOF on a background thread, passing chunks to `queue`,
    /// then leaves `done` (which the caller entered).
    func read(_ handle: FileHandle, done: DispatchGroup, onData: @escaping (Data) -> Void) {
        let queue = self.queue
        DispatchQueue.global(qos: .utility).async {
            while true {
                let data = handle.availableData
                if data.isEmpty { break }
                queue.async { onData(data) }
            }
            queue.async { done.leave() }
        }
    }

    func output(_ data: Data, of current: Run) {
        guard !current.cancelled else { return }
        let events = current.parser.feed(data)
        events.forEach(current.deliver)
        guard let last = events.last, last.isTerminal else { return }
        // Claude Code exits by itself after a successful result; after a failure
        // (or if it lingers) it's stopped.
        queue.asyncAfter(deadline: .now() + (last == .finished ? 5 : 0)) {
            Self.stop(current)
        }
    }

    /// SIGTERM to the process and everything below it, SIGKILL to what's left after 3 s.
    static func stop(_ current: Run) {
        guard current.process.isRunning, !current.cancelled else { return }
        current.cancelled = true
        let root = current.process.processIdentifier
        let ownGroup = current.ownGroup
        let tree = ProcessTree.descendants(of: root) + [root]
        let signal = { (sig: Int32) in
            if ownGroup { kill(-root, sig) }
            tree.filter { kill($0, 0) == 0 }.forEach { kill($0, sig) }
        }
        signal(SIGTERM)
        DispatchQueue.global().asyncAfter(deadline: .now() + 3) { signal(SIGKILL) }
    }

    func exited(_ current: Run) {
        if current.ownGroup {
            // Whatever Claude Code left running (normally nothing).
            kill(-current.process.processIdentifier, SIGTERM)
        }
        if !current.cancelled {
            current.parser.finish().forEach(current.deliver)
        }
        if current.cancelled {
            current.deliver(.failed("Cancelled"))
        } else if current.process.terminationReason == .uncaughtSignal {
            current.deliver(.failed("Claude Code was stopped (signal \(current.process.terminationStatus))."))
        } else if current.process.terminationStatus != 0 {
            current.deliver(.failed(Self.exitMessage(status: current.process.terminationStatus, stderr: current.errorTail)))
        } else {
            current.deliver(.failed("Claude Code stopped without finishing the study plan."))
        }
        finish(current)
    }

    func finish(_ current: Run) {
        try? FileManager.default.removeItem(at: current.folder)
        if run === current {
            run = nil
        }
    }

    static func exitMessage(status: Int32, stderr: Data) -> String {
        let lastLine = String(decoding: stderr, as: UTF8.self)
            .split(separator: "\n")
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .last { !$0.isEmpty }
        return "Claude Code exited with status \(status)" + (lastLine.map { ": \($0)" } ?? ".")
    }
}

// MARK: - Command line

extension ClaudeCodeRunner {
    /// Whiteprint's MCP tools, plus reading the `whiteprint://dsl` resource.
    static let allowedTools = "mcp__whiteprint__*,ReadMcpResourceTool"

    static func prompt(importIDs: [String]) -> String {
        let imports = importIDs.isEmpty ? "all" : importIDs.joined(separator: ", ")
        return WhiteprintText.studyPlanPrompt + "\n\nImports: \(imports)"
    }

    /// Anything not allowed is refused without asking (`dontAsk`), so Claude
    /// can't wander off into the user's files or shell.
    static func arguments(prompt: String, configPath: String) -> [String] {
        [
            "-p", prompt,
            "--output-format", "stream-json",
            "--verbose",
            "--strict-mcp-config",
            "--mcp-config", configPath,
            "--allowedTools", allowedTools,
            "--permission-mode", "dontAsk",
            "--no-session-persistence",
        ]
    }

    static func mcpConfig(helperURL: URL) throws -> Data {
        let config = ["mcpServers": ["whiteprint": ["command": helperURL.path, "args": [String]()] as [String: Any]]]
        return try JSONSerialization.data(withJSONObject: config, options: [.prettyPrinted, .sortedKeys])
    }

    /// The app's environment with the login shell's PATH, so Claude Code finds
    /// whatever it needs as if started from Terminal.
    static func environment(claudeURL: URL) -> [String: String] {
        var environment = ProcessInfo.processInfo.environment
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        var path = (LoginShell.shared.path ?? environment["PATH"] ?? "/usr/bin:/bin:/usr/sbin:/sbin")
            .split(separator: ":").map(String.init)
        for extra in [claudeURL.deletingLastPathComponent().path, "\(home)/.local/bin", "/opt/homebrew/bin", "/usr/local/bin"]
        where !path.contains(extra) {
            path.append(extra)
        }
        environment["PATH"] = path.joined(separator: ":")
        return environment
    }
}

/// What the user's login shell reports, looked up once (`zsh -l` can be slow).
final class LoginShell {
    static let shared = LoginShell()

    private let lock = NSLock()
    private var answer: (path: String?, claude: String?)?

    var path: String? { lookup().path }
    var claudePath: String? { lookup().claude }

    private func lookup() -> (path: String?, claude: String?) {
        lock.lock()
        defer { lock.unlock() }
        if let answer { return answer }
        // Markers keep any output from the user's profile scripts out of the answer.
        let output = Self.run("printf '\\n@wp-path %s\\n@wp-claude %s\\n' \"$PATH\" \"$(command -v claude)\"", timeout: 5) ?? ""
        var found: (path: String?, claude: String?) = (nil, nil)
        for line in output.split(separator: "\n") {
            if line.hasPrefix("@wp-path ") {
                found.path = String(line.dropFirst(9))
            } else if line.hasPrefix("@wp-claude /") {
                found.claude = String(line.dropFirst(11))
            }
        }
        if found.path?.isEmpty ?? true {
            found.path = nil
        }
        answer = found
        return found
    }

    /// Runs `command` in a login zsh. Nil if it fails or takes longer than `timeout`.
    static func run(_ command: String, timeout: TimeInterval) -> String? {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/zsh")
        process.arguments = ["-lc", command]
        process.standardInput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        let pipe = Pipe()
        process.standardOutput = pipe
        let exited = DispatchSemaphore(value: 0)
        process.terminationHandler = { _ in exited.signal() }
        do {
            try process.run()
        } catch {
            return nil
        }
        var output = Data()
        let readDone = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            output = pipe.fileHandleForReading.readDataToEndOfFile()
            readDone.signal()
        }
        let deadline = DispatchTime.now() + timeout
        guard exited.wait(timeout: deadline) == .success, readDone.wait(timeout: deadline) == .success else {
            process.terminate()
            return nil
        }
        return String(decoding: output, as: UTF8.self)
    }
}

enum ProcessTree {
    /// Every process below `root`, deepest first.
    static func descendants(of root: pid_t) -> [pid_t] {
        var children: [pid_t: [pid_t]] = [:]
        for (pid, parent) in parents() {
            children[parent, default: []].append(pid)
        }
        var result: [pid_t] = []
        var queue = children[root] ?? []
        while let pid = queue.first {
            queue.removeFirst()
            result.append(pid)
            queue += children[pid] ?? []
        }
        return result.reversed()
    }

    /// Parent pid of every running process.
    private static func parents() -> [pid_t: pid_t] {
        var mib: [Int32] = [CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0]
        var size = 0
        guard sysctl(&mib, 4, nil, &size, nil, 0) == 0 else { return [:] }
        let capacity = size / MemoryLayout<kinfo_proc>.stride + 32
        var procs = [kinfo_proc](repeating: kinfo_proc(), count: capacity)
        size = capacity * MemoryLayout<kinfo_proc>.stride
        guard sysctl(&mib, 4, &procs, &size, nil, 0) == 0 else { return [:] }
        var parents: [pid_t: pid_t] = [:]
        for proc in procs.prefix(size / MemoryLayout<kinfo_proc>.stride) {
            parents[proc.kp_proc.p_pid] = proc.kp_eproc.e_ppid
        }
        return parents
    }
}
