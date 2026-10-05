import Foundation
import XCTest
import WhiteprintCore
@testable import WhiteprintBridge

final class MCPToolsTests: XCTestCase {
    private var sent: [BridgeRequest] = []
    private var server: MCPServer!

    override func setUp() {
        sent = []
        server = MCPServer { [unowned self] request in
            self.sent.append(request)
            return .ok("done")
        }
    }

    /// Calls a tool through JSON-RPC; returns the result's text and isError flag.
    private func call(_ name: String, _ arguments: Any) throws -> (text: String, isError: Bool) {
        let message: [String: Any] = ["jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": ["name": name, "arguments": arguments]]
        let line = String(decoding: try JSONSerialization.data(withJSONObject: message), as: UTF8.self)
        let reply = try XCTUnwrap(server.handle(line: line))
        let object = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(reply.utf8)) as? [String: Any])
        let result = try XCTUnwrap(object["result"] as? [String: Any], "\(object)")
        let content = try XCTUnwrap(result["content"] as? [[String: Any]])
        return (try XCTUnwrap(content.first?["text"] as? String), try XCTUnwrap(result["isError"] as? Bool))
    }

    private func request(_ name: String, _ arguments: [String: Any]) throws -> BridgeRequest? {
        sent = []
        let r = try call(name, arguments)
        XCTAssertFalse(r.isError, "\(name): \(r.text)")
        XCTAssertEqual(r.text, "done")
        XCTAssertLessThanOrEqual(sent.count, 1)
        return sent.first
    }

    private func invalid(_ name: String, _ arguments: Any) throws -> String {
        sent = []
        let r = try call(name, arguments)
        XCTAssertTrue(r.isError, "\(name) accepted \(arguments)")
        XCTAssertTrue(sent.isEmpty)
        XCTAssertFalse(r.text.contains("\n"))
        return r.text
    }

    private func toolList() throws -> [[String: Any]] {
        let reply = try XCTUnwrap(server.handle(line: #"{"jsonrpc":"2.0","id":1,"method":"tools/list"}"#))
        let object = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(reply.utf8)) as? [String: Any])
        return try XCTUnwrap((object["result"] as? [String: Any])?["tools"] as? [[String: Any]])
    }

    // MARK: tools/list

    func testListsExactlyTheFifteenTools() throws {
        XCTAssertEqual(try toolList().compactMap { $0["name"] as? String }, [
            "list_notes", "read_note", "create_note", "write", "draw", "edit_drawing", "delete_drawing", "add_page",
            "import_document", "list_imports", "read_chunk", "save_points", "get_points", "build_study_plan",
            "create_flashcards",
        ])
    }

    func testToolListMatchesTheCatalog() throws {
        let listed = try toolList()
        let catalog = MCPToolCatalog.tools
        XCTAssertEqual(listed.count, catalog.count)
        for (json, tool) in zip(listed, catalog) {
            XCTAssertEqual(json["name"] as? String, tool.name)
            XCTAssertEqual(json["description"] as? String, tool.description)
            XCTAssertEqual(json["annotations"] as? [String: Bool], tool.annotations)
            XCTAssertTrue(NSDictionary(dictionary: json["inputSchema"] as? [String: Any] ?? [:]).isEqual(to: tool.inputSchema), tool.name)
        }
    }

    func testToolListSize() throws {
        let reply = try XCTUnwrap(server.handle(line: #"{"jsonrpc":"2.0","id":1,"method":"tools/list"}"#))
        XCTAssertLessThan(reply.utf8.count, 6000)
    }

    func testSchemasAreWellFormed() throws {
        for tool in try toolList() {
            let name = tool["name"] as? String ?? "?"
            let description = try XCTUnwrap(tool["description"] as? String)
            XCTAssertFalse(description.isEmpty)
            XCTAssertLessThanOrEqual(description.count, 90, name)
            let annotations = try XCTUnwrap(tool["annotations"] as? [String: Any], name)
            XCTAssertFalse(annotations.isEmpty, name)
            try checkSchema(XCTUnwrap(tool["inputSchema"] as? [String: Any], name), at: name, root: true)
        }
    }

    private func checkSchema(_ schema: [String: Any], at path: String, root: Bool = false) throws {
        let type = try XCTUnwrap(schema["type"] as? String, path)
        XCTAssertTrue(["object", "array", "string", "integer"].contains(type), path)
        if root { XCTAssertEqual(type, "object", path) }
        switch type {
        case "object":
            let properties = try XCTUnwrap(schema["properties"] as? [String: [String: Any]], path)
            for (name, property) in properties {
                try checkSchema(property, at: "\(path).\(name)")
            }
            for name in schema["required"] as? [String] ?? [] {
                XCTAssertNotNil(properties[name], "\(path) requires undeclared \(name)")
            }
        case "array":
            try checkSchema(XCTUnwrap(schema["items"] as? [String: Any], path), at: "\(path)[]")
        default:
            if let values = schema["enum"] { XCTAssertFalse((values as? [String] ?? []).isEmpty, path) }
        }
    }

    func testAnnotations() throws {
        let hints = Dictionary(uniqueKeysWithValues: try toolList().map { ($0["name"] as! String, $0["annotations"] as! [String: Bool]) })
        for name in ["list_notes", "read_note", "list_imports", "read_chunk", "get_points"] {
            XCTAssertEqual(hints[name], ["readOnlyHint": true], name)
        }
        for name in ["write", "edit_drawing", "delete_drawing"] {
            XCTAssertEqual(hints[name], ["destructiveHint": true], name)
        }
        for name in ["create_note", "draw", "add_page", "import_document", "save_points", "build_study_plan", "create_flashcards"] {
            XCTAssertEqual(hints[name], ["destructiveHint": false], name)
        }
    }

    func testDrawPointsToTheDSLResource() throws {
        let draw = try XCTUnwrap(try toolList().first { $0["name"] as? String == "draw" })
        XCTAssertTrue((draw["description"] as? String ?? "").contains("whiteprint://dsl"))
    }

    // MARK: argument → request mapping

    func testNoteTools() throws {
        XCTAssertEqual(try request("list_notes", [:]), .listNotes)
        XCTAssertEqual(try request("read_note", ["note": "n1"]), .readNote(note: "n1", page: nil))
        XCTAssertEqual(try request("read_note", ["note": "n1", "page": 2]), .readNote(note: "n1", page: 2))
        XCTAssertEqual(try request("create_note", ["title": "T"]), .createNote(title: "T", markdown: nil, folder: nil))
        XCTAssertEqual(try request("create_note", ["title": "T", "markdown": "# Hi"]), .createNote(title: "T", markdown: "# Hi", folder: nil))
        XCTAssertEqual(try request("write", ["note": "n1", "page": 1, "markdown": "x", "mode": "append"]),
                       .write(note: "n1", page: 1, markdown: "x", mode: .append))
        XCTAssertEqual(try request("write", ["note": "n1", "page": 3, "markdown": "", "mode": "replace"]),
                       .write(note: "n1", page: 3, markdown: "", mode: .replace))
        XCTAssertEqual(try request("draw", ["note": "n1", "page": 1, "dsl": "box a"]),
                       .draw(note: "n1", page: 1, dsl: "box a", after: nil))
        XCTAssertEqual(try request("draw", ["note": "n1", "page": 1, "dsl": "box a", "after": "d2"]),
                       .draw(note: "n1", page: 1, dsl: "box a", after: "d2"))
        XCTAssertEqual(try request("edit_drawing", ["note": "n1", "drawing": "d1", "dsl": "box b"]),
                       .editDrawing(note: "n1", drawing: "d1", dsl: "box b"))
        XCTAssertEqual(try request("delete_drawing", ["note": "n1", "drawing": "d1"]), .deleteDrawing(note: "n1", drawing: "d1"))
        XCTAssertEqual(try request("add_page", ["note": "n1"]), .addPage(note: "n1"))
    }

    func testStudyTools() throws {
        XCTAssertEqual(try request("import_document", ["path": "/tmp/a.pdf"]), .importDocument(path: "/tmp/a.pdf"))
        XCTAssertEqual(try request("list_imports", [:]), .listImports)
        XCTAssertEqual(try request("read_chunk", ["import": "i1", "chunk": 0]), .readChunk(importID: "i1", chunk: 0))
        XCTAssertEqual(try request("get_points", [:]), .getPoints(importID: nil))
        XCTAssertEqual(try request("get_points", ["import": "i2"]), .getPoints(importID: "i2"))
        let points: [[String: Any]] = [
            ["text": "TCP is reliable", "importance": "must", "ref": "L1.pdf · p. 3", "topic": "Transport"],
            ["text": "History of ARPANET", "importance": "skip", "ref": "L1.pdf · p. 1"],
        ]
        XCTAssertEqual(try request("save_points", ["import": "i1", "chunk": 2, "points": points]), .savePoints(importID: "i1", chunk: 2, points: [
            StudyPoint(text: "TCP is reliable", importance: .must, ref: "L1.pdf · p. 3", topic: "Transport"),
            StudyPoint(text: "History of ARPANET", importance: .skip, ref: "L1.pdf · p. 1"),
        ]))
        XCTAssertEqual(try request("save_points", ["import": "i1", "chunk": 3, "points": []]),
                       .savePoints(importID: "i1", chunk: 3, points: []))
    }

    func testBuildStudyPlan() throws {
        let plan: [String: Any] = [
            "title": "Networks",
            "overview": "Layers.",
            "modules": [
                ["title": "Transport", "minutes": 45, "points": [["text": "TCP", "importance": "must", "ref": "L1 · p. 3"]]],
                ["title": "Extras", "points": []],
            ],
            "tasks": [["text": "Lab 1", "due": "week 2", "ref": "Syllabus · p. 2"], ["text": "Read ch. 3"]],
            "diagrams": ["flow A>B"],
        ]
        XCTAssertEqual(try request("build_study_plan", ["imports": ["i1", "i2"], "plan": plan]), .buildStudyPlan(
            importIDs: ["i1", "i2"],
            plan: StudyPlan(
                title: "Networks", overview: "Layers.",
                modules: [
                    StudyModule(title: "Transport", minutes: 45, points: [StudyPoint(text: "TCP", importance: .must, ref: "L1 · p. 3")]),
                    StudyModule(title: "Extras", points: []),
                ],
                tasks: [StudyTask(text: "Lab 1", due: "week 2", ref: "Syllabus · p. 2"), StudyTask(text: "Read ch. 3")],
                diagrams: ["flow A>B"])))
    }

    func testStudyPlanTasksAndDiagramsAreOptional() throws {
        let plan: [String: Any] = ["title": "T", "overview": "O", "modules": []]
        XCTAssertEqual(try request("build_study_plan", ["imports": [], "plan": plan]),
                       .buildStudyPlan(importIDs: [], plan: StudyPlan(title: "T", overview: "O", modules: [])))
    }

    func testCreateNoteInFolder() throws {
        XCTAssertEqual(try request("create_note", ["title": "T", "folder": "Courses/Networks"]),
                       .createNote(title: "T", markdown: nil, folder: "Courses/Networks"))
        XCTAssertEqual(try request("create_note", ["title": "T", "folder": " Courses/ "]),
                       .createNote(title: "T", markdown: nil, folder: "Courses"))
        XCTAssertEqual(try request("create_note", ["title": "T", "folder": ""]), .createNote(title: "T", markdown: nil, folder: nil))
        for folder in ["/Users/x", "~/Notes", "../Secrets", "a/../../b"] {
            XCTAssertEqual(try invalid("create_note", ["title": "T", "folder": folder]),
                           "Invalid arguments: folder: use a path inside the notes folder, e.g. Courses/Networks", folder)
        }
    }

    func testCreateFlashcards() throws {
        let cards: [[String: Any]] = [
            ["question": "What does TCP guarantee?", "answer": "Ordered, reliable delivery.", "ref": "L3.pptx · slide 4"],
            ["question": "UDP?", "answer": "Datagrams."],
        ]
        let expected = [
            Flashcard(question: "What does TCP guarantee?", answer: "Ordered, reliable delivery.", ref: "L3.pptx · slide 4"),
            Flashcard(question: "UDP?", answer: "Datagrams."),
        ]
        XCTAssertEqual(try request("create_flashcards", ["title": "TCP", "cards": cards]),
                       .createFlashcards(note: nil, page: nil, title: "TCP", cards: expected))
        XCTAssertEqual(try request("create_flashcards", ["note": "n4", "page": "2", "title": "TCP", "cards": cards]),
                       .createFlashcards(note: "n4", page: 2, title: "TCP", cards: expected))
        XCTAssertEqual(try invalid("create_flashcards", ["title": "TCP", "cards": []]),
                       "Invalid arguments: cards: add at least one card")
        XCTAssertEqual(try invalid("create_flashcards", ["title": "TCP", "cards": [["question": "Q"]]]),
                       "Invalid arguments: cards[0].answer: required")
        XCTAssertEqual(try invalid("create_flashcards", ["cards": cards]), "Invalid arguments: title: required")
    }

    func testStudyPlanFlashcards() throws {
        let plan: [String: Any] = [
            "title": "T", "overview": "O", "modules": [],
            "flashcards": [["question": "Q", "answer": "A", "ref": "p. 1"], ["question": "Q2", "answer": "A2"]],
        ]
        XCTAssertEqual(try request("build_study_plan", ["imports": ["i1"], "plan": plan]), .buildStudyPlan(
            importIDs: ["i1"],
            plan: StudyPlan(title: "T", overview: "O", modules: [],
                            flashcards: [Flashcard(question: "Q", answer: "A", ref: "p. 1"), Flashcard(question: "Q2", answer: "A2")])))
        XCTAssertEqual(try invalid("build_study_plan", ["imports": [], "plan": [
            "title": "T", "overview": "O", "modules": [], "flashcards": [["question": "Q"]],
        ]]), "Invalid arguments: plan.flashcards[0].answer: required")
    }

    // MARK: catalog

    func testCatalogMapsLikeToolsCall() throws {
        let calls: [(String, [String: Any])] = [
            ("list_notes", [:]), ("read_note", ["note": "n1", "page": 2]), ("create_note", ["title": "T", "folder": "A"]),
            ("write", ["note": "n1", "page": 1, "markdown": "x", "mode": "replace"]),
            ("draw", ["note": "n1", "page": 1, "dsl": "box a"]), ("edit_drawing", ["note": "n1", "drawing": "d1", "dsl": "x"]),
            ("delete_drawing", ["note": "n1", "drawing": "d1"]), ("add_page", ["note": "n1"]),
            ("import_document", ["path": "/tmp/a.pdf"]), ("list_imports", [:]), ("read_chunk", ["import": "i1", "chunk": 1]),
            ("save_points", ["import": "i1", "chunk": 1, "points": []]), ("get_points", ["import": "i1"]),
            ("build_study_plan", ["imports": [], "plan": ["title": "T", "overview": "O", "modules": []]]),
            ("create_flashcards", ["title": "T", "cards": [["question": "Q", "answer": "A"]]]),
        ]
        XCTAssertEqual(calls.map(\.0), MCPToolCatalog.tools.map(\.name))
        for (name, arguments) in calls {
            let viaServer = try request(name, arguments)
            XCTAssertEqual(try MCPToolCatalog.request(forTool: name, arguments: arguments), viaServer, name)
        }
    }

    func testCatalogErrorsAreOneLine() {
        XCTAssertThrowsError(try MCPToolCatalog.request(forTool: "nope", arguments: [:])) { error in
            XCTAssertEqual(error as? ToolCallError, .unknownTool("nope"))
            XCTAssertEqual("\(error)", "unknown tool: nope")
        }
        XCTAssertThrowsError(try MCPToolCatalog.request(forTool: "read_chunk", arguments: ["import": "i1", "chunk": "x"])) { error in
            XCTAssertEqual("\(error)", "Invalid arguments: chunk: expected an integer")
        }
    }

    func testLenientIntegersAndNulls() throws {
        XCTAssertEqual(try request("read_note", ["note": "n1", "page": "2"]), .readNote(note: "n1", page: 2))
        XCTAssertEqual(try request("read_note", ["note": "n1", "page": 2.0]), .readNote(note: "n1", page: 2))
        XCTAssertEqual(try request("read_note", ["note": "n1", "page": NSNull()]), .readNote(note: "n1", page: nil))
    }

    // MARK: validation

    func testValidationErrors() throws {
        XCTAssertEqual(try invalid("read_note", [:]), "Invalid arguments: note: required")
        XCTAssertEqual(try invalid("read_note", ["note": 5]), "Invalid arguments: note: expected a string")
        XCTAssertEqual(try invalid("read_note", ["note": "n1", "page": 1.5]), "Invalid arguments: page: expected an integer")
        XCTAssertEqual(try invalid("read_note", ["note": "n1", "page": true]), "Invalid arguments: page: expected an integer")
        XCTAssertEqual(try invalid("write", ["note": "n1", "page": 1, "markdown": "x", "mode": "insert"]),
                       "Invalid arguments: mode: expected one of append, replace")
        XCTAssertEqual(try invalid("write", ["note": "n1", "page": 1, "markdown": "x"]), "Invalid arguments: mode: required")
        XCTAssertEqual(try invalid("create_note", ["title": "T", "md": "x"]),
                       "Invalid arguments: md: unknown argument (expected title, markdown, folder)")
        XCTAssertEqual(try invalid("list_notes", [1, 2]), "Invalid arguments: expected an object")
        XCTAssertEqual(try invalid("save_points", ["import": "i1", "chunk": 1, "points": "x"]),
                       "Invalid arguments: points: expected an array")
        XCTAssertEqual(try invalid("save_points", ["import": "i1", "chunk": 1, "points": [["text": "a", "importance": "high", "ref": "r"]]]),
                       "Invalid arguments: points[0].importance: expected one of must, good, skip")
        XCTAssertEqual(try invalid("save_points", ["import": "i1", "chunk": 1, "points": [["text": "a", "importance": "must"]]]),
                       "Invalid arguments: points[0].ref: required")
        XCTAssertEqual(try invalid("build_study_plan", ["imports": ["i1"], "plan": ["title": "T", "overview": "O"]]),
                       "Invalid arguments: plan.modules: required")
        XCTAssertEqual(try invalid("build_study_plan", ["imports": [1], "plan": ["title": "T", "overview": "O", "modules": []]]),
                       "Invalid arguments: imports[0]: expected a string")
        XCTAssertEqual(try invalid("build_study_plan", ["imports": [], "plan": [
            "title": "T", "overview": "O", "modules": [["title": "M", "minutes": "soon", "points": []]],
        ]]), "Invalid arguments: plan.modules[0].minutes: expected an integer")
    }
}
