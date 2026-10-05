/// The friendly progress line shown while an agent runs a tool, shared by
/// `ClaudeCodeRunner` and `GrokRunner`.
enum ToolStatus {
    static let mcpPrefix = "mcp__whiteprint__"

    /// E.g. "Reading chunk 3 of 12 · Lecture3.pptx". `tool` may carry Claude
    /// Code's `mcp__whiteprint__` prefix; `importInfo` names imports.
    static func text(tool: String, input: [String: Any], importInfo: (String) -> StudyImport?) -> String {
        let name = tool.hasPrefix(mcpPrefix) ? String(tool.dropFirst(mcpPrefix.count)) : tool
        switch name {
        case "list_imports": return "Looking at your imports…"
        case "read_chunk": return reading(input, importInfo: importInfo)
        case "save_points": return "Saving points…"
        case "get_points": return "Merging the points…"
        case "build_study_plan": return "Building the study plan…"
        case "create_flashcards": return "Making flashcards…"
        case "ReadMcpResourceTool", "ListMcpResourcesTool": return "Reading the drawing reference…"
        default: return "Working…"
        }
    }

    private static func reading(_ input: [String: Any], importInfo: (String) -> StudyImport?) -> String {
        let id = (input["import"] ?? input["importID"] ?? input["id"]) as? String
        let n = (input["n"] ?? input["chunk"]) as? Int
        guard let id, let n else { return "Reading…" }
        guard let info = importInfo(id) else { return "Reading chunk \(n) · \(id)" }
        return "Reading chunk \(n) of \(info.chunkCount) · \(info.name)"
    }
}
