import Foundation
import XCTest
@testable import WhiteprintApp
import WhiteprintBridge
import WhiteprintCore
import WhiteprintStudy

/// Notes held in memory, keyed by file URL, as the bridge sees them.
private final class MemoryWorkspace: NoteWorkspace {
    var notes: [(url: URL, note: Note)] = []
    var edits: [String] = []
    let folder = URL(fileURLWithPath: "/tmp/whiteprint-tests", isDirectory: true)

    func add(_ name: String, _ text: String) throws -> URL {
        let url = folder.appendingPathComponent(name)
        notes.append((url, try Note(parsing: text)))
        return url
    }

    func noteURLs() -> [URL] {
        notes.map(\.url)
    }

    func note(at url: URL) throws -> Note {
        guard let note = notes.first(where: { $0.url == url })?.note else { throw WorkspaceError.fileNotFound(url.path) }
        return note
    }

    func edit<T>(noteAt url: URL, actionName: String, _ change: (inout Note) throws -> T) throws -> T {
        guard let i = notes.firstIndex(where: { $0.url == url }) else { throw WorkspaceError.fileNotFound(url.path) }
        var note = notes[i].note
        let result = try change(&note)
        notes[i].note = note
        edits.append(actionName)
        return result
    }

    func folderPath(of url: URL) -> String? {
        NotesFolder(url: folder).relativeFolder(of: url)
    }

    func createNote(_ note: Note, title: String, folder path: String?) throws -> URL {
        let directory = try path.map(NotesFolder(url: folder).folderURL(forPath:)) ?? folder
        let url = directory.appendingPathComponent(title + ".wprint")
        notes.append((url, note))
        return url
    }
}

final class BridgeServiceTests: XCTestCase {
    private var workspace: MemoryWorkspace!
    private var registry: NoteRegistry!
    private var service: BridgeService!
    private var studyDirectory: URL!

    override func setUpWithError() throws {
        workspace = MemoryWorkspace()
        registry = NoteRegistry()
        studyDirectory = FileManager.default.temporaryDirectory.appendingPathComponent("wp-study-\(UUID().uuidString)")
        service = BridgeService(workspace: workspace, registry: registry, study: try StudyStore(directory: studyDirectory))
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: studyDirectory)
    }

    private func send(_ request: BridgeRequest) async -> BridgeResponse {
        await withCheckedContinuation { continuation in
            service.handle(request) { continuation.resume(returning: $0) }
        }
    }

    private func ok(_ request: BridgeRequest) async throws -> String {
        let response = await send(request)
        guard case .ok(let text) = response else {
            XCTFail("expected ok, got \(response)")
            throw CocoaError(.featureUnsupported)
        }
        return text
    }

    private func listedID(_ title: String) async throws -> String {
        let list = try await ok(.listNotes)
        let line = try XCTUnwrap(list.split(separator: "\n").first { $0.contains("\"\(title)\"") })
        return String(line.split(separator: " ")[0])
    }

    func testListNotesAssignsStableIDs() async throws {
        _ = try workspace.add("Alpha.wprint", "---\nwhiteprint: 1\ntitle: Networks\n---\nOne\n+++page\nTwo")
        _ = try workspace.add("Beta.wprint", "Hello")
        let first = try await ok(.listNotes)
        XCTAssertEqual(first, "n1 \"Networks\" · 2 pages\nn2 \"Beta\" · 1 page")
        workspace.notes.reverse()
        let second = try await ok(.listNotes)
        XCTAssertEqual(second, "n2 \"Beta\" · 1 page\nn1 \"Networks\" · 2 pages")
    }

    func testListNotesWhenEmpty() async throws {
        let text = try await ok(.listNotes)
        XCTAssertEqual(text, "no notes")
    }

    func testReadNoteShowsPageHeadersForAllPages() async throws {
        _ = try workspace.add("A.wprint", "One\n\n```wp\nbox a\n```\n+++page\nTwo")
        let id = try await listedID("A")
        let all = try await ok(.readNote(note: id, page: nil))
        XCTAssertEqual(all, "=== page 1 ===\nOne\n\n```wp id=d1\nbox a\n```\n\n=== page 2 ===\nTwo")
        let second = try await ok(.readNote(note: id, page: 2))
        XCTAssertEqual(second, "Two")
    }

    func testUnknownNoteAndPageFailWithShortErrors() async throws {
        _ = try workspace.add("A.wprint", "One")
        let id = try await listedID("A")
        let unknown = await send(.readNote(note: "n9", page: nil))
        XCTAssertEqual(unknown, .failure("no note 'n9' (see list_notes)"))
        let page = await send(.write(note: id, page: 3, markdown: "x", mode: .append))
        XCTAssertEqual(page, .failure("page 3 doesn't exist (note has 1 page)"))
    }

    func testWriteGoesThroughTheWorkspace() async throws {
        let url = try workspace.add("A.wprint", "One")
        let id = try await listedID("A")
        let reply = try await ok(.write(note: id, page: 1, markdown: "Two", mode: .append))
        XCTAssertEqual(reply, "ok")
        XCTAssertEqual(try workspace.note(at: url).pages[0].blocks, [.text("One"), .text("Two")])
        XCTAssertEqual(workspace.edits.count, 1)
    }

    func testDrawReturnsIDAndCompileErrors() async throws {
        let url = try workspace.add("A.wprint", "One")
        let id = try await listedID("A")
        let clean = try await ok(.draw(note: id, page: 1, dsl: "flow a>b", after: nil))
        XCTAssertEqual(clean, "ok d1")
        let broken = try await ok(.draw(note: id, page: 1, dsl: "box a\narrow a>x", after: "d1"))
        let lines = broken.split(separator: "\n").map(String.init)
        XCTAssertEqual(lines.first, "ok d2")
        XCTAssertEqual(lines.count, 2)
        XCTAssertTrue(lines[1].hasPrefix("line 2: "), lines[1])
        XCTAssertEqual(try workspace.note(at: url).drawings.map(\.id), ["d1", "d2"])
    }

    func testDrawingWhereNothingCompilesIsNotSaved() async throws {
        let url = try workspace.add("A.wprint", "```wp id=d1\nbox a\n```")
        let id = try await listedID("A")
        let draw = await send(.draw(note: id, page: 1, dsl: "flow a>", after: nil))
        XCTAssertEqual(draw, .failure("nothing to draw, not saved:\nline 1: bad link 'a>', use a>b"))
        let edit = await send(.editDrawing(note: id, drawing: "d1", dsl: "squiggle"))
        XCTAssertEqual(edit, .failure("nothing to draw, not saved:\nline 1: unknown command 'squiggle'"))
        XCTAssertEqual(try workspace.note(at: url).drawings, [Drawing(id: "d1", source: "box a")])
    }

    func testEditAndDeleteDrawing() async throws {
        let url = try workspace.add("A.wprint", "```wp id=d1\nbox a\n```")
        let id = try await listedID("A")
        let edited = try await ok(.editDrawing(note: id, drawing: "d1", dsl: "box b"))
        XCTAssertEqual(edited, "ok")
        XCTAssertEqual(try workspace.note(at: url).drawing("d1")?.drawing.source, "box b")
        let deleted = try await ok(.deleteDrawing(note: id, drawing: "d1"))
        XCTAssertEqual(deleted, "ok")
        let missing = await send(.deleteDrawing(note: id, drawing: "d1"))
        XCTAssertEqual(missing, .failure("no drawing 'd1' in this note"))
    }

    func testAddPageReturnsItsNumber() async throws {
        _ = try workspace.add("A.wprint", "One")
        let id = try await listedID("A")
        let reply = try await ok(.addPage(note: id))
        XCTAssertEqual(reply, "ok 2")
    }

    func testCreateNoteReturnsNewID() async throws {
        let reply = try await ok(.createNote(title: "Plan", markdown: "# Plan\n+++page\nMore", folder: nil))
        XCTAssertEqual(reply, "ok n1")
        let note = try workspace.note(at: try XCTUnwrap(registry.url(for: "n1")))
        XCTAssertEqual(note.frontMatter.title, "Plan")
        XCTAssertEqual(note.pages.count, 2)
    }

    func testListNotesShowsFolders() async throws {
        _ = try workspace.add("Top.wprint", "One")
        _ = try workspace.add("Courses/Networks/TCP.wprint", "One\n+++page\nTwo")
        let list = try await ok(.listNotes)
        XCTAssertEqual(list, "n1 \"Top\" · 1 page\nn2 \"TCP\" · Courses/Networks · 2 pages")
    }

    func testCreateNoteInFolder() async throws {
        let reply = try await ok(.createNote(title: "TCP", markdown: nil, folder: "Courses/Networks"))
        XCTAssertEqual(reply, "ok n1")
        let url = try XCTUnwrap(registry.url(for: "n1"))
        XCTAssertEqual(workspace.folderPath(of: url), "Courses/Networks")
        for bad in ["../Elsewhere", "/etc", "Courses/../../x", "~/Notes"] {
            let refused = await send(.createNote(title: "X", markdown: nil, folder: bad))
            guard case .failure(let message) = refused else { return XCTFail("\(bad) was accepted") }
            XCTAssertTrue(message.contains("must be a path inside the notes folder"), message)
        }
    }

    func testCreateFlashcardsMakesANewNote() async throws {
        let cards = [Flashcard(question: "What does TCP guarantee?", answer: "Ordered delivery", ref: "Lecture3.pptx · slide 4")]
        let reply = try await ok(.createFlashcards(note: nil, page: nil, title: "TCP", cards: cards))
        XCTAssertEqual(reply, "ok n1 c1")
        let url = try XCTUnwrap(registry.url(for: "n1"))
        XCTAssertEqual(url.lastPathComponent, "Flashcards – TCP.wprint")
        XCTAssertEqual(try workspace.note(at: url).decks, [CardDeck(id: "c1", title: "TCP", cards: cards)])
    }

    func testCreateFlashcardsAddsToTheLastPageByDefault() async throws {
        let url = try workspace.add("A.wprint", "One\n+++page\nTwo")
        let id = try await listedID("A")
        let cards = [Flashcard(question: "Q", answer: "A")]
        let first = try await ok(.createFlashcards(note: id, page: nil, title: "Deck", cards: cards))
        XCTAssertEqual(first, "ok \(id) c1")
        let second = try await ok(.createFlashcards(note: id, page: 1, title: "Other", cards: cards))
        XCTAssertEqual(second, "ok \(id) c2")
        let note = try workspace.note(at: url)
        XCTAssertEqual(note.deck("c1")?.page, 2)
        XCTAssertEqual(note.deck("c2")?.page, 1)
        let empty = await send(.createFlashcards(note: id, page: nil, title: "Empty", cards: []))
        XCTAssertEqual(empty, .failure("no cards given; each card needs a question and an answer"))
        let badPage = await send(.createFlashcards(note: id, page: 7, title: "Deck", cards: cards))
        XCTAssertEqual(badPage, .failure("page 7 doesn't exist (note has 2 pages)"))
    }

    func testImportValidatesPathAndType() async throws {
        let missing = await send(.importDocument(path: "/nonexistent/Lecture.pdf"))
        XCTAssertEqual(missing, .failure("no file at /nonexistent/Lecture.pdf"))
        let text = FileManager.default.temporaryDirectory.appendingPathComponent("wp-\(UUID().uuidString).txt")
        try "hi".write(to: text, atomically: true, encoding: .utf8)
        defer { try? FileManager.default.removeItem(at: text) }
        let wrongType = await send(.importDocument(path: text.path))
        XCTAssertEqual(wrongType, .failure("\(text.lastPathComponent): unsupported file type (use PDF, DOCX, DOC or PPTX)"))
    }

    func testBuildStudyPlanRejectsUnknownImports() async throws {
        let plan = StudyPlan(title: "Networks", overview: "", modules: [])
        let reply = await send(.buildStudyPlan(importIDs: ["i42"], plan: plan))
        XCTAssertEqual(reply, .failure("no import 'i42'"))
    }

    func testBuildStudyPlanCreatesNote() async throws {
        let plan = StudyPlan(title: "Networks", overview: "Covers routing.", modules: [])
        let reply = try await ok(.buildStudyPlan(importIDs: [], plan: plan))
        XCTAssertEqual(reply, "ok n1")
        XCTAssertEqual(registry.url(for: "n1")?.lastPathComponent, "Study plan – Networks.wprint")
    }

    func testImportSummary() {
        let item = StudyImport(id: "i2", name: "Lecture3.pptx", unitCount: 14, chunkCount: 3, chunksDone: 0, importedAt: Date())
        XCTAssertEqual(BridgeService.summary(of: item), "i2 · 14 slides · 3 chunks")
        let pdf = StudyImport(id: "i3", name: "Syllabus.PDF", unitCount: 1, chunkCount: 1, chunksDone: 1, importedAt: Date())
        XCTAssertEqual(BridgeService.summary(of: pdf, includingID: false), "1 page · 1 chunk")
    }

    func testPing() async throws {
        let reply = try await ok(.ping)
        XCTAssertEqual(reply, "pong")
    }
}

/// Records requests and answers them from a table.
private final class FakeHandler: BridgeHandler {
    var requests: [BridgeRequest] = []
    var response = BridgeResponse.ok("ok")

    func handle(_ request: BridgeRequest, reply: @escaping (BridgeResponse) -> Void) {
        requests.append(request)
        reply(response)
    }
}

final class AgentToolBridgeTests: XCTestCase {
    private func run(_ executor: GrokRunner.ToolExecutor, _ name: String, _ arguments: [String: Any]) async -> (String, Bool) {
        await withCheckedContinuation { continuation in
            executor(name, arguments) { text, isError in continuation.resume(returning: (text, isError)) }
        }
    }

    func testMapsToolsOneToOne() {
        let tools = [AgentTool(name: "list_notes", description: "List notes", inputSchema: ["type": "object"])]
        let mapped = AgentToolBridge.runnerTools(tools)
        XCTAssertEqual(mapped.map(\.name), ["list_notes"])
        XCTAssertEqual(mapped.map(\.description), ["List notes"])
        XCTAssertEqual(mapped.first?.inputSchema["type"] as? String, "object")
    }

    func testExecutorRunsRequestsThroughTheHandler() async {
        let handler = FakeHandler()
        let executor = AgentToolBridge.executor(handler: handler) { name, arguments in
            guard name == "read_note", let note = arguments["note"] as? String else {
                throw WorkspaceError.unknownNote(name)
            }
            return .readNote(note: note, page: nil)
        }
        let ok = await run(executor, "read_note", ["note": "n1"])
        XCTAssertEqual(ok.0, "ok")
        XCTAssertFalse(ok.1)
        XCTAssertEqual(handler.requests, [.readNote(note: "n1", page: nil)])

        handler.response = .failure("no note 'n1' (see list_notes)")
        let failed = await run(executor, "read_note", ["note": "n1"])
        XCTAssertEqual(failed.0, "no note 'n1' (see list_notes)")
        XCTAssertTrue(failed.1)

        let invalid = await run(executor, "bogus", [:])
        XCTAssertEqual(invalid.0, "no note 'bogus' (see list_notes)")
        XCTAssertTrue(invalid.1)
        XCTAssertEqual(handler.requests.count, 2)
    }
}
