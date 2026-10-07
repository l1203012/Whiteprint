import Foundation
import XCTest
@testable import WhiteprintApp
import WhiteprintCore

final class FolderTreeTests: XCTestCase {
    private var folder: NotesFolder!

    override func setUpWithError() throws {
        folder = NotesFolder(url: FileManager.default.temporaryDirectory.appendingPathComponent("wp-tree-\(UUID().uuidString)", isDirectory: true))
        try folder.create()
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: folder.url)
    }

    private func touch(_ path: String) throws -> URL {
        let url = folder.url.appendingPathComponent(path)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try "x".write(to: url, atomically: true, encoding: .utf8)
        return url.canonicalFile
    }

    func testListingIsRecursiveTopLevelFirst() throws {
        for path in ["b.wprint", "Courses/Networks/TCP.wprint", "Courses/Intro.wprint", "A.wprint", "Courses/notes.txt", ".hidden/x.wprint"] {
            _ = try touch(path)
        }
        try FileManager.default.createDirectory(at: folder.url.appendingPathComponent("Empty"), withIntermediateDirectories: true)
        let listing = folder.listing()
        XCTAssertEqual(listing.notes.map { folder.relativePath(of: $0)! }, ["A.wprint", "b.wprint", "Courses/Intro.wprint", "Courses/Networks/TCP.wprint"])
        XCTAssertEqual(listing.folders, ["Courses", "Courses/Networks", "Empty"])
    }

    func testRelativeFolders() throws {
        let top = try touch("A.wprint")
        let nested = try touch("Courses/Networks/TCP.wprint")
        XCTAssertEqual(folder.relativeFolder(of: top), "")
        XCTAssertEqual(folder.relativeFolder(of: nested), "Courses/Networks")
        XCTAssertNil(folder.relativeFolder(of: URL(fileURLWithPath: "/elsewhere/A.wprint")))
        XCTAssertTrue(folder.contains(nested))
        XCTAssertFalse(folder.contains(folder.url))
    }

    func testFolderPathsMustStayInside() throws {
        XCTAssertEqual(try folder.folderURL(forPath: " Courses//Networks/ ").path, folder.url.canonicalFile.appendingPathComponent("Courses/Networks").path)
        XCTAssertEqual(try folder.folderURL(forPath: "").path, folder.url.canonicalFile.path)
        for bad in ["..", "../x", "a/../../b", "/tmp", "~/x", "./a", "a:b"] {
            XCTAssertThrowsError(try folder.folderURL(forPath: bad), bad)
        }
    }

    func testCreateFolderCountsUp() throws {
        let first = try folder.createFolder()
        let second = try folder.createFolder()
        XCTAssertEqual(first.lastPathComponent, "New Folder")
        XCTAssertEqual(second.lastPathComponent, "New Folder 2")
        let nested = try folder.createFolder(named: "Week 1", in: first)
        XCTAssertEqual(folder.relativePath(of: nested), "New Folder/Week 1")
    }

    func testSaveIntoSubfolder() throws {
        let url = try folder.save(Note(title: "TCP"), title: "TCP", in: try folder.folderURL(forPath: "Courses/Networks"))
        XCTAssertEqual(folder.relativeFolder(of: url), "Courses/Networks")
    }

    func testMoveKeepsNamesUnique() throws {
        let note = try touch("A.wprint")
        _ = try touch("Courses/A.wprint")
        let courses = folder.url.appendingPathComponent("Courses", isDirectory: true)
        let moved = try NoteFiles.move(note, into: courses)
        XCTAssertEqual(folder.relativePath(of: moved), "Courses/A 2.wprint")
        XCTAssertFalse(FileManager.default.fileExists(atPath: note.path))
        XCTAssertEqual(try NoteFiles.move(moved, into: courses).path, moved.path)
    }

    func testFolderCantMoveIntoItself() throws {
        _ = try touch("Courses/Networks/TCP.wprint")
        let courses = folder.url.appendingPathComponent("Courses", isDirectory: true)
        XCTAssertThrowsError(try NoteFiles.move(courses, into: courses.appendingPathComponent("Networks")))
        XCTAssertThrowsError(try NoteFiles.move(courses, into: courses))
        let moved = try NoteFiles.move(courses.appendingPathComponent("Networks"), into: folder.url)
        XCTAssertEqual(folder.relativePath(of: moved), "Networks")
        XCTAssertTrue(FileManager.default.fileExists(atPath: moved.appendingPathComponent("TCP.wprint").path))
    }

    func testRename() throws {
        let note = try touch("Courses/A.wprint")
        _ = try touch("Courses/Taken.wprint")
        let renamed = try NoteFiles.rename(note, to: "TCP/IP")
        XCTAssertEqual(folder.relativePath(of: renamed), "Courses/TCP-IP.wprint")
        XCTAssertThrowsError(try NoteFiles.rename(renamed, to: "Taken"))
        XCTAssertThrowsError(try NoteFiles.rename(renamed, to: "  "))
        let courses = folder.url.appendingPathComponent("Courses", isDirectory: true)
        let folderRenamed = try NoteFiles.rename(courses, to: "Classes")
        XCTAssertEqual(folder.relativePath(of: folderRenamed), "Classes")
    }

    func testRelocated() {
        let old = URL(fileURLWithPath: "/notes/Courses", isDirectory: true)
        let new = URL(fileURLWithPath: "/notes/Archive/Courses", isDirectory: true)
        XCTAssertEqual(NoteFiles.relocated(URL(fileURLWithPath: "/notes/Courses/Net/TCP.wprint"), from: old, to: new)?.path,
                       "/notes/Archive/Courses/Net/TCP.wprint")
        XCTAssertNil(NoteFiles.relocated(URL(fileURLWithPath: "/notes/CoursesOld/TCP.wprint"), from: old, to: new))
    }

    func testRegistryFollowsFolderMoves() {
        let registry = NoteRegistry()
        let tcp = URL(fileURLWithPath: "/notes/Courses/TCP.wprint")
        let other = URL(fileURLWithPath: "/notes/Other.wprint")
        _ = registry.id(for: tcp)
        _ = registry.id(for: other)
        registry.move(from: URL(fileURLWithPath: "/notes/Courses"), to: URL(fileURLWithPath: "/notes/Archive/Courses"))
        XCTAssertEqual(registry.url(for: "n1")?.path, "/notes/Archive/Courses/TCP.wprint")
        XCTAssertEqual(registry.id(for: URL(fileURLWithPath: "/notes/Archive/Courses/TCP.wprint")), "n1")
        XCTAssertEqual(registry.url(for: "n2")?.path, other.path)
    }

    func testTreeOrdersFoldersFirst() {
        func entry(_ name: String, _ folder: String?) -> NoteEntry {
            NoteEntry(url: URL(fileURLWithPath: "/notes/\(folder ?? "x")/\(name).wprint"), title: name, pageCount: 1, folder: folder)
        }
        let tree = NoteTree.build(
            entries: [entry("A", ""), entry("TCP", "Courses/Networks"), entry("Intro", "Courses"), entry("Outside", nil), entry("B", "")],
            folders: ["Zeta", "Courses", "Courses/Networks", "alpha"]
        )
        XCTAssertEqual(tree.folders.map(\.name), ["alpha", "Courses", "Zeta"])
        XCTAssertEqual(tree.notes.map(\.title), ["A", "B", "Outside"])
        let courses = tree.folders[1]
        XCTAssertEqual(courses.path, "Courses")
        XCTAssertEqual(courses.folders.map(\.path), ["Courses/Networks"])
        XCTAssertEqual(courses.notes.map(\.title), ["Intro"])
        XCTAssertEqual(courses.folders[0].notes.map(\.title), ["TCP"])
    }

    func testOnlyFoldersWithContentsExpand() {
        let note = NoteEntry(url: URL(fileURLWithPath: "/notes/Courses/Networks/TCP.wprint"), title: "TCP", pageCount: 1, folder: "Courses/Networks")
        let tree = NoteTree.build(entries: [note], folders: ["Courses", "Courses/Networks", "Empty"])
        let nodes = SidebarViewController.nodes(for: tree, root: URL(fileURLWithPath: "/notes", isDirectory: true))
        XCTAssertEqual(nodes.map(\.folderPath), ["Courses", "Empty"])
        XCTAssertTrue(nodes[0].isExpandable, "a folder holding only a subfolder")
        XCTAssertTrue(nodes[0].children[0].isExpandable, "a folder holding a note")
        XCTAssertFalse(nodes[1].isExpandable, "an empty folder")
        XCTAssertFalse(nodes[0].children[0].children[0].isExpandable, "a note")
        XCTAssertTrue(SidebarNode(.section("Notes")).isExpandable, "an empty section")
    }

    func testMergedListsOpenNotesElsewhereLast() {
        let a = URL(fileURLWithPath: "/notes/A.wprint"), b = URL(fileURLWithPath: "/elsewhere/B.wprint")
        XCTAssertEqual(NotesLibrary.merged([a], open: [a, b]), [a, b])
    }
}
