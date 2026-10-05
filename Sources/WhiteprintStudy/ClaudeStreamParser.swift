import Foundation

/// Turns Claude Code's `--output-format stream-json` output (one JSON object
/// per line) into runner events. Feed it bytes as they arrive; partial lines
/// are buffered until their newline. Lines that aren't JSON are ignored.
public struct ClaudeStreamParser {
    public typealias Event = ClaudeCodeRunner.Event

    /// Looks up an import for progress messages like "Reading chunk 3 of 12 · Lecture3.pptx".
    private let importInfo: (String) -> StudyImport?
    private var buffer = Data()
    /// True once a `result` message (or a fatal init problem) was seen.
    public private(set) var isFinished = false

    public init(importInfo: @escaping (String) -> StudyImport? = { _ in nil }) {
        self.importInfo = importInfo
    }

    public mutating func feed(_ data: Data) -> [Event] {
        buffer.append(data)
        var events: [Event] = []
        while let newline = buffer.firstIndex(of: UInt8(ascii: "\n")) {
            let line = buffer[buffer.startIndex..<newline]
            events += parse(line: Data(line))
            buffer.removeSubrange(buffer.startIndex...newline)
        }
        return events
    }

    /// Parses whatever is left after the stream closed.
    public mutating func finish() -> [Event] {
        defer { buffer = Data() }
        return buffer.isEmpty ? [] : parse(line: buffer)
    }

    public mutating func parse(line: Data) -> [Event] {
        guard !isFinished,
              let object = try? JSONSerialization.jsonObject(with: line),
              let message = object as? [String: Any] else { return [] }
        switch message["type"] as? String {
        case "system": return system(message)
        case "assistant": return assistant(message)
        case "result": return [result(message)]
        default: return []
        }
    }
}

private extension ClaudeStreamParser {
    mutating func system(_ message: [String: Any]) -> [Event] {
        guard message["subtype"] as? String == "init" else { return [] }
        let servers = message["mcp_servers"] as? [[String: Any]] ?? []
        if let whiteprint = servers.first(where: { $0["name"] as? String == "whiteprint" }),
           whiteprint["status"] as? String == "failed" {
            isFinished = true
            return [.failed("Claude Code couldn't start Whiteprint's tools. Try reinstalling Whiteprint.")]
        }
        return [.status("Starting Claude…")]
    }

    func assistant(_ message: [String: Any]) -> [Event] {
        let body = message["message"] as? [String: Any]
        let content = body?["content"] as? [[String: Any]] ?? []
        return content.compactMap { block -> Event? in
            switch block["type"] as? String {
            case "text":
                let text = (block["text"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
                return text.isEmpty ? nil : .text(text)
            case "tool_use":
                return .status(ToolStatus.text(tool: block["name"] as? String ?? "", input: block["input"] as? [String: Any] ?? [:],
                                               importInfo: importInfo))
            default:
                return nil
            }
        }
    }

    mutating func result(_ message: [String: Any]) -> Event {
        isFinished = true
        let isError = message["is_error"] as? Bool ?? false
        let subtype = message["subtype"] as? String ?? ""
        if !isError && subtype == "success" {
            return .finished
        }
        let text = (message["result"] as? String ?? (message["errors"] as? [String])?.first ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        if !text.isEmpty {
            return .failed(text)
        }
        switch subtype {
        case "error_max_turns": return .failed("Claude stopped after too many steps. Run it again to continue.")
        case "error_max_budget_usd": return .failed("Claude stopped at its spending limit.")
        default: return .failed("Claude Code ran into an error (\(subtype.isEmpty ? "unknown" : subtype)).")
        }
    }
}
