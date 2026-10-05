import Foundation
import WhiteprintCore

/// One MCP tool: what Claude sees in `tools/list`, and how its arguments become
/// a `BridgeRequest`. Descriptions are sent on every turn, so they stay terse.
struct MCPTool {
    enum Effect {
        case readOnly, additive, destructive
    }

    let name: String
    let description: String
    let arguments: [ToolSchema.Property]
    let effect: Effect
    let request: (ToolArguments) throws -> BridgeRequest

    var json: [String: Any] {
        let hint: [String: Any]
        switch effect {
        case .readOnly: hint = ["readOnlyHint": true]
        case .additive: hint = ["destructiveHint": false]
        case .destructive: hint = ["destructiveHint": true]
        }
        return ["name": name, "description": description, "inputSchema": ToolSchema.object(arguments).json, "annotations": hint]
    }

    /// Validates raw `arguments` and maps them to the request for the app.
    func makeRequest(_ arguments: [String: Any]) throws -> BridgeRequest {
        let valid = try ToolSchema.object(self.arguments).validate(arguments)
        return try request(ToolArguments(values: valid as? [String: Any] ?? [:]))
    }
}

/// Validated, normalised tool arguments.
struct ToolArguments {
    let values: [String: Any]

    func string(_ key: String) throws -> String {
        guard let value = values[key] as? String else { throw ArgumentError(key, "required") }
        return value
    }

    func optionalString(_ key: String) -> String? {
        values[key] as? String
    }

    func int(_ key: String) throws -> Int {
        guard let value = values[key] as? Int else { throw ArgumentError(key, "required") }
        return value
    }

    func optionalInt(_ key: String) -> Int? {
        values[key] as? Int
    }

    /// Decodes a validated object or array into a core model type.
    func decode<T: Decodable>(_ type: T.Type, _ key: String, defaults: [String: Any] = [:]) throws -> T {
        var value = values[key] ?? NSNull()
        if var object = value as? [String: Any] {
            object.merge(defaults) { current, _ in current }
            value = object
        }
        do {
            let data = try JSONSerialization.data(withJSONObject: value)
            return try JSONDecoder().decode(type, from: data)
        } catch {
            throw ArgumentError(key, "invalid \(type)")
        }
    }
}

extension MCPTool {
    private static let point = ToolSchema.object([
        .required("text", .string),
        .required("importance", .oneOf(Importance.allCases.map(\.rawValue))),
        .required("ref", .string),
        .optional("topic", .string),
    ])

    private static let plan = ToolSchema.object([
        .required("title", .string),
        .required("overview", .string),
        .required("modules", .array(.object([
            .required("title", .string),
            .optional("minutes", .integer),
            .required("points", .array(point)),
        ]))),
        .optional("tasks", .array(.object([
            .required("text", .string),
            .optional("due", .string),
            .optional("ref", .string),
        ]))),
        .optional("diagrams", .array(.string)),
    ])

    static let all: [MCPTool] = [
        MCPTool(name: "list_notes", description: "List notes: id, title, pages.",
                arguments: [], effect: .readOnly) { _ in .listNotes },
        MCPTool(name: "read_note", description: "Read a note's source, or one page of it.",
                arguments: [.required("note", .string), .optional("page", .integer)], effect: .readOnly) {
            .readNote(note: try $0.string("note"), page: $0.optionalInt("page"))
        },
        MCPTool(name: "create_note", description: "Create a note. Returns its id.",
                arguments: [.required("title", .string), .optional("markdown", .string)], effect: .additive) {
            .createNote(title: try $0.string("title"), markdown: $0.optionalString("markdown"), folder: nil)
        },
        MCPTool(name: "write", description: "Append to or replace a page's markdown.",
                arguments: [
                    .required("note", .string), .required("page", .integer), .required("markdown", .string),
                    .required("mode", .oneOf(["append", "replace"])),
                ], effect: .destructive) {
            .write(note: try $0.string("note"), page: try $0.int("page"), markdown: try $0.string("markdown"),
                   mode: WriteMode(rawValue: try $0.string("mode")) ?? .append)
        },
        MCPTool(name: "draw", description: "Add a drawing to a page. Language: resource whiteprint://dsl. Returns its id.",
                arguments: [
                    .required("note", .string), .required("page", .integer), .required("dsl", .string),
                    .optional("after", .string),
                ], effect: .additive) {
            .draw(note: try $0.string("note"), page: try $0.int("page"), dsl: try $0.string("dsl"),
                  after: $0.optionalString("after"))
        },
        MCPTool(name: "edit_drawing", description: "Replace a drawing's source.",
                arguments: [.required("note", .string), .required("drawing", .string), .required("dsl", .string)],
                effect: .destructive) {
            .editDrawing(note: try $0.string("note"), drawing: try $0.string("drawing"), dsl: try $0.string("dsl"))
        },
        MCPTool(name: "delete_drawing", description: "Delete a drawing.",
                arguments: [.required("note", .string), .required("drawing", .string)], effect: .destructive) {
            .deleteDrawing(note: try $0.string("note"), drawing: try $0.string("drawing"))
        },
        MCPTool(name: "add_page", description: "Add a page at the end. Returns its number.",
                arguments: [.required("note", .string)], effect: .additive) {
            .addPage(note: try $0.string("note"))
        },
        MCPTool(name: "import_document", description: "Import a PDF, Word or PowerPoint file for study. Returns its id.",
                arguments: [.required("path", .string)], effect: .additive) {
            .importDocument(path: try $0.string("path"))
        },
        MCPTool(name: "list_imports", description: "List imports: id, name, pages, chunks, status.",
                arguments: [], effect: .readOnly) { _ in .listImports },
        MCPTool(name: "read_chunk", description: "Read one chunk of an import, lines tagged with source refs.",
                arguments: [.required("import", .string), .required("chunk", .integer)], effect: .readOnly) {
            .readChunk(importID: try $0.string("import"), chunk: try $0.int("chunk"))
        },
        MCPTool(name: "save_points", description: "Save the key points found in a chunk.",
                arguments: [.required("import", .string), .required("chunk", .integer), .required("points", .array(point))],
                effect: .additive) {
            .savePoints(importID: try $0.string("import"), chunk: try $0.int("chunk"),
                        points: try $0.decode([StudyPoint].self, "points"))
        },
        MCPTool(name: "get_points", description: "List saved points, for all imports or one.",
                arguments: [.optional("import", .string)], effect: .readOnly) {
            .getPoints(importID: $0.optionalString("import"))
        },
        MCPTool(name: "build_study_plan", description: "Write the final study plan as a new note. Returns its id.",
                arguments: [.required("imports", .array(.string)), .required("plan", plan)], effect: .additive) {
            .buildStudyPlan(importIDs: try $0.decode([String].self, "imports"),
                            plan: try $0.decode(StudyPlan.self, "plan", defaults: ["tasks": [Any](), "diagrams": [Any]()]))
        },
    ]

    static func named(_ name: String) -> MCPTool? {
        all.first { $0.name == name }
    }
}
