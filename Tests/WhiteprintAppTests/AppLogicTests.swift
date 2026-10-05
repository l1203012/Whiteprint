import Foundation
import XCTest
@testable import WhiteprintApp
import WhiteprintCore

final class NoteRegistryTests: XCTestCase {
    func testIDsAreSequentialAndStable() {
        let registry = NoteRegistry()
        let a = URL(fileURLWithPath: "/notes/A.wprint")
        let b = URL(fileURLWithPath: "/notes/B.wprint")
        XCTAssertEqual(registry.id(for: a), "n1")
        XCTAssertEqual(registry.id(for: b), "n2")
        XCTAssertEqual(registry.id(for: URL(fileURLWithPath: "/notes/./A.wprint")), "n1")
        XCTAssertEqual(registry.url(for: "n2"), b)
        XCTAssertEqual(registry.url(for: " N2 "), b)
        XCTAssertNil(registry.url(for: "n3"))
    }

    func testIDFollowsRenamedFile() {
        let registry = NoteRegistry()
        let old = URL(fileURLWithPath: "/notes/A.wprint")
        let new = URL(fileURLWithPath: "/notes/Renamed.wprint")
        _ = registry.id(for: old)
        registry.move(from: old, to: new)
        XCTAssertEqual(registry.id(for: new), "n1")
        XCTAssertEqual(registry.url(for: "n1"), new)
        XCTAssertEqual(registry.id(for: old), "n2")
    }
}

final class NotesFolderTests: XCTestCase {
    private var folder: NotesFolder!

    override func setUpWithError() throws {
        folder = NotesFolder(url: FileManager.default.temporaryDirectory.appendingPathComponent("wp-notes-\(UUID().uuidString)", isDirectory: true))
        try folder.create()
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: folder.url)
    }

    func testListsOnlyNotesSortedByName() throws {
        for name in ["b.wprint", "A 10.wprint", "A 2.wprint", "notes.txt", ".hidden.wprint"] {
            try "x".write(to: folder.url.appendingPathComponent(name), atomically: true, encoding: .utf8)
        }
        XCTAssertEqual(folder.noteURLs().map(\.lastPathComponent), ["A 2.wprint", "A 10.wprint", "b.wprint"])
    }

    func testUnusedURLCountsUp() throws {
        XCTAssertEqual(folder.unusedURL(forTitle: "Untitled").lastPathComponent, "Untitled.wprint")
        _ = try folder.save(Note(), title: "Untitled")
        XCTAssertEqual(folder.unusedURL(forTitle: "Untitled").lastPathComponent, "Untitled 2.wprint")
        _ = try folder.save(Note(), title: "Untitled")
        XCTAssertEqual(folder.unusedURL(forTitle: "Untitled").lastPathComponent, "Untitled 3.wprint")
    }

    func testSaveWritesTheNote() throws {
        let note = Note(title: "Plan")
        let url = try folder.save(note, title: "Plan")
        XCTAssertTrue(folder.contains(url))
        XCTAssertEqual(try Note(parsing: String(contentsOf: url, encoding: .utf8)), note)
    }

    func testFileNamesAreSafe() {
        XCTAssertEqual(NotesFolder.fileName(forTitle: "TCP/IP: basics"), "TCP-IP- basics")
        XCTAssertEqual(NotesFolder.fileName(forTitle: "..hidden"), "hidden")
        XCTAssertEqual(NotesFolder.fileName(forTitle: "  "), "Untitled")
        XCTAssertEqual(NotesFolder.fileName(forTitle: String(repeating: "a", count: 300)).count, 100)
    }

    func testFolderOverrides() {
        let defaults = UserDefaults(suiteName: "wp-tests-\(UUID().uuidString)")!
        XCTAssertEqual(NotesFolder.current(defaults: defaults, environment: [:]).url, NotesFolder.defaultURL)
        defaults.set("/tmp/chosen", forKey: NotesFolder.defaultsKey)
        XCTAssertEqual(NotesFolder.current(defaults: defaults, environment: [:]).url.path, "/tmp/chosen")
        let env = [NotesFolder.environmentKey: "/tmp/env"]
        XCTAssertEqual(NotesFolder.current(defaults: defaults, environment: env).url.path, "/tmp/env")
    }
}

final class NoteTitleTests: XCTestCase {
    func testTitleFallsBackToFileName() throws {
        let url = URL(fileURLWithPath: "/notes/Lecture 3.wprint")
        XCTAssertEqual(NoteTitle.display(for: Note(title: "Networks"), fileURL: url), "Networks")
        XCTAssertEqual(NoteTitle.display(for: Note(title: "  "), fileURL: url), "Lecture 3")
        XCTAssertEqual(NoteTitle.display(for: Note(), fileURL: url), "Lecture 3")
        XCTAssertEqual(NoteTitle.display(for: Note(), fileURL: nil), "Untitled")
    }

    func testRenameChangesTitleOnlyWhenTheNameChanged() {
        let a = URL(fileURLWithPath: "/notes/A.wprint")
        XCTAssertEqual(NoteTitle.titleAfterRename(from: a, to: URL(fileURLWithPath: "/notes/B.wprint")), "B")
        XCTAssertNil(NoteTitle.titleAfterRename(from: a, to: URL(fileURLWithPath: "/elsewhere/A.wprint")))
        XCTAssertNil(NoteTitle.titleAfterRename(from: nil, to: a))
    }

    func testPageTitles() throws {
        let note = try Note(parsing: "Intro text\n# Overview\n+++page\n\n## Routing basics\n+++page\n```wp\nbox a\n```")
        XCTAssertEqual(PageOutline.titles(of: note), ["Overview", "Routing basics", "Page 3"])
    }

    func testWelcomeNoteParses() throws {
        let note = try Note(parsing: WelcomeNote.source)
        XCTAssertEqual(note.frontMatter.title, "Welcome")
        XCTAssertEqual(note.pages.count, 2)
        XCTAssertEqual(note.drawings.map(\.source), ["flow You>Whiteprint>Claude\ntext \"Claude writes and draws in your notes, on your own subscription\""])
        XCTAssertEqual(DrawingCompiler.compile(note.drawings[0].source).errors, [])
    }
}

final class PaletteSearchTests: XCTestCase {
    func testRanking() {
        let titles = ["Insert drawing", "New note", "Networks", "Generate study plan", "Settings"]
        XCTAssertEqual(PaletteSearch.filter(titles, query: "ne", title: { $0 }), ["New note", "Networks", "Generate study plan", "Insert drawing"])
        XCTAssertEqual(PaletteSearch.filter(titles, query: "draw", title: { $0 }), ["Insert drawing"])
        XCTAssertEqual(PaletteSearch.filter(titles, query: "gsp", title: { $0 }), ["Generate study plan"])
        XCTAssertEqual(PaletteSearch.filter(titles, query: "", title: { $0 }), titles)
        XCTAssertEqual(PaletteSearch.filter(titles, query: "zzz", title: { $0 }), [])
    }
}

final class ClaudeSetupTests: XCTestCase {
    private let helper = URL(fileURLWithPath: "/Applications/Whiteprint.app/Contents/MacOS/whiteprint-mcp")
    private var directory: URL!

    override func setUpWithError() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent("wp-claude-\(UUID().uuidString)", isDirectory: true)
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: directory)
    }

    private func object(_ data: Data) throws -> [String: Any] {
        try XCTUnwrap(try JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    func testMergeKeepsOtherServersAndKeys() throws {
        let existing = Data(#"{"mcpServers":{"other":{"command":"/bin/other","args":["x"]}},"theme":"dark"}"#.utf8)
        let merged = try object(ClaudeDesktopConfig.merging(helper: helper, into: existing))
        XCTAssertEqual(merged["theme"] as? String, "dark")
        let servers = try XCTUnwrap(merged["mcpServers"] as? [String: Any])
        XCTAssertEqual((servers["other"] as? [String: Any])?["command"] as? String, "/bin/other")
        XCTAssertEqual((servers["whiteprint"] as? [String: Any])?["command"] as? String, helper.path)
    }

    func testMergeIntoNothing() throws {
        for input in [nil, Data(), Data(" \n".utf8)] {
            let merged = try object(ClaudeDesktopConfig.merging(helper: helper, into: input))
            XCTAssertEqual(merged.keys.sorted(), ["mcpServers"])
        }
    }

    func testMergeRefusesNonObjects() {
        XCTAssertThrowsError(try ClaudeDesktopConfig.merging(helper: helper, into: Data("[1]".utf8)))
        XCTAssertThrowsError(try ClaudeDesktopConfig.merging(helper: helper, into: Data("{broken".utf8)))
    }

    func testConnectWritesBackupFirst() throws {
        let config = directory.appendingPathComponent("Claude/claude_desktop_config.json")
        try FileManager.default.createDirectory(at: config.deletingLastPathComponent(), withIntermediateDirectories: true)
        let original = Data(#"{"mcpServers":{"other":{"command":"/bin/other"}}}"#.utf8)
        try original.write(to: config)
        XCTAssertFalse(ClaudeDesktopConfig.isConnected(helper: helper, configURL: config))

        let backup = try XCTUnwrap(try ClaudeDesktopConfig.connect(helper: helper, configURL: config))
        XCTAssertEqual(backup.lastPathComponent, "claude_desktop_config.json.backup")
        XCTAssertEqual(try Data(contentsOf: backup), original)
        XCTAssertTrue(ClaudeDesktopConfig.isConnected(helper: helper, configURL: config))
        let servers = try XCTUnwrap(try object(Data(contentsOf: config))["mcpServers"] as? [String: Any])
        XCTAssertEqual(servers.keys.sorted(), ["other", "whiteprint"])
    }

    func testConnectCreatesMissingConfig() throws {
        let config = directory.appendingPathComponent("Claude/claude_desktop_config.json")
        XCTAssertNil(try ClaudeDesktopConfig.connect(helper: helper, configURL: config))
        XCTAssertTrue(ClaudeDesktopConfig.isConnected(helper: helper, configURL: config))
    }

    func testBrokenConfigIsLeftAlone() throws {
        let config = directory.appendingPathComponent("claude_desktop_config.json")
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try Data("{broken".utf8).write(to: config)
        XCTAssertThrowsError(try ClaudeDesktopConfig.connect(helper: helper, configURL: config))
        XCTAssertEqual(try Data(contentsOf: config), Data("{broken".utf8))
    }

    func testClaudeCodeCommand() {
        XCTAssertEqual(ClaudeCodeSetup.arguments(helper: helper), ["mcp", "add", "--scope", "user", "whiteprint", "--", helper.path])
        let spaced = URL(fileURLWithPath: "/Users/me/My Apps/Whiteprint.app/Contents/MacOS/whiteprint-mcp")
        XCTAssertEqual(
            ClaudeCodeSetup.commandLine(claude: nil, helper: spaced),
            "claude mcp add --scope user whiteprint -- '/Users/me/My Apps/Whiteprint.app/Contents/MacOS/whiteprint-mcp'"
        )
        XCTAssertEqual(ClaudeCodeSetup.shellQuoted("it's"), #"'it'\''s'"#)
    }

    func testErrorLines() {
        XCTAssertEqual(errorLine(NoteEditError.lastPage), "can't remove the only page")
        XCTAssertEqual(errorLine(NoteFormatError.unsupportedVersion(9)), "note uses format 9; this Whiteprint reads up to 1")
        XCTAssertEqual(errorLine(CocoaError(.fileNoSuchFile)), CocoaError(.fileNoSuchFile).localizedDescription)
    }
}
