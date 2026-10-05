import Foundation

/// The MCP server name Whiteprint registers under, in both Claude clients.
let mcpServerName = "whiteprint"

/// Adds the Whiteprint helper to Claude Desktop's config file.
enum ClaudeDesktopConfig {
    enum ConfigError: Error, CustomStringConvertible {
        case notAnObject

        var description: String {
            "claude_desktop_config.json isn't a JSON object, so it was left unchanged"
        }
    }

    /// `~/Library/Application Support/Claude/claude_desktop_config.json`.
    static var defaultURL: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Claude/claude_desktop_config.json")
    }

    /// `config` (nil or empty when there's no file yet) with
    /// `mcpServers.whiteprint` set to the helper. Other servers and keys are kept.
    static func merging(helper: URL, into config: Data?) throws -> Data {
        var root: [String: Any] = [:]
        if let config, !config.allSatisfy({ $0 == 0x20 || $0 == 0x0A || $0 == 0x0D || $0 == 0x09 }) {
            guard let object = try JSONSerialization.jsonObject(with: config) as? [String: Any] else {
                throw ConfigError.notAnObject
            }
            root = object
        }
        var servers = root["mcpServers"] as? [String: Any] ?? [:]
        servers[mcpServerName] = ["command": helper.path]
        root["mcpServers"] = servers
        return try JSONSerialization.data(withJSONObject: root, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
    }

    /// Whether the config already starts this helper.
    static func isConnected(helper: URL, configURL: URL = defaultURL) -> Bool {
        guard let data = try? Data(contentsOf: configURL),
              let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let servers = root["mcpServers"] as? [String: Any],
              let entry = servers[mcpServerName] as? [String: Any] else { return false }
        return entry["command"] as? String == helper.path
    }

    /// Merges the helper into the config file, copying the old file to
    /// `<name>.backup` first. Returns the backup's URL, if there was a file.
    @discardableResult
    static func connect(helper: URL, configURL: URL = defaultURL) throws -> URL? {
        let fm = FileManager.default
        let existing = fm.fileExists(atPath: configURL.path) ? try Data(contentsOf: configURL) : nil
        let merged = try merging(helper: helper, into: existing)
        var backup: URL?
        if existing != nil {
            let url = configURL.appendingPathExtension("backup")
            if fm.fileExists(atPath: url.path) { try fm.removeItem(at: url) }
            try fm.copyItem(at: configURL, to: url)
            backup = url
        }
        try fm.createDirectory(at: configURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        try merged.write(to: configURL, options: .atomic)
        return backup
    }

    /// The snippet to paste by hand.
    static func snippet(helper: URL) -> String {
        let data = (try? merging(helper: helper, into: nil)) ?? Data()
        return String(decoding: data, as: UTF8.self)
    }
}

/// Registers the helper with Claude Code (`claude mcp add`).
enum ClaudeCodeSetup {
    /// `mcp add --scope user whiteprint -- <helper>`.
    static func arguments(helper: URL) -> [String] {
        ["mcp", "add", "--scope", "user", mcpServerName, "--", helper.path]
    }

    /// The same command as a line to paste into Terminal.
    static func commandLine(claude: URL?, helper: URL) -> String {
        ([claude?.path ?? "claude"] + arguments(helper: helper)).map(shellQuoted).joined(separator: " ")
    }

    static func shellQuoted(_ word: String) -> String {
        let safe = CharacterSet.alphanumerics.union(CharacterSet(charactersIn: "-_./=:@%+,"))
        if !word.isEmpty, word.unicodeScalars.allSatisfy(safe.contains) { return word }
        return "'" + word.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }

    /// Runs `claude mcp add …` off the main thread and reports its output on the main queue.
    static func register(claude: URL, helper: URL, completion: @escaping (Result<String, Error>) -> Void) {
        DispatchQueue.global(qos: .userInitiated).async {
            let process = Process()
            process.executableURL = claude
            process.arguments = arguments(helper: helper)
            let pipe = Pipe()
            process.standardOutput = pipe
            process.standardError = pipe
            let result: Result<String, Error>
            do {
                try process.run()
                let output = String(decoding: pipe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                process.waitUntilExit()
                result = process.terminationStatus == 0
                    ? .success(output)
                    : .failure(SetupError.failed(output.isEmpty ? "claude exited with status \(process.terminationStatus)" : output))
            } catch {
                result = .failure(error)
            }
            DispatchQueue.main.async { completion(result) }
        }
    }

    enum SetupError: LocalizedError {
        case failed(String)

        var errorDescription: String? {
            switch self {
            case .failed(let output): return output
            }
        }
    }
}
