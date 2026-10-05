import Foundation
import XCTest
import WhiteprintCore
import WhiteprintExtract
@testable import WhiteprintStudy

final class StudyStoreTests: XCTestCase {
    private var root: URL!

    override func setUp() {
        root = FileManager.default.temporaryDirectory.appendingPathComponent("StudyStoreTests-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: root)
    }

    private var storeURL: URL { root.appendingPathComponent("Study") }

    /// Fake extractor: every line of the file is `ref|text`.
    private static func extract(_ url: URL) throws -> ExtractedDocument {
        let text = try String(contentsOf: url)
        let units = text.split(separator: "\n").map { line -> ExtractedUnit in
            let parts = line.split(separator: "|", maxSplits: 1)
            return ExtractedUnit(ref: String(parts[0]), text: String(parts[1]))
        }
        return ExtractedDocument(name: url.lastPathComponent, units: units)
    }

    /// Fake chunker: two units per chunk.
    private static func chunk(_ document: ExtractedDocument) -> [ExtractedChunk] {
        stride(from: 0, to: document.units.count, by: 2).map { start in
            let units = document.units[start..<min(start + 2, document.units.count)]
            return ExtractedChunk(
                index: start / 2 + 1, firstRef: units.first!.ref, lastRef: units.last!.ref,
                text: units.map { "[\($0.ref)]\n\($0.text)" }.joined(separator: "\n")
            )
        }
    }

    private func makeStore() throws -> StudyStore {
        try StudyStore(directory: storeURL, extract: Self.extract, chunk: Self.chunk)
    }

    private func file(_ name: String, units: Int, salt: String = "") throws -> URL {
        let url = root.appendingPathComponent(name)
        let lines = (1...units).map { "slide \($0)|Content \($0)\(salt)" }
        try lines.joined(separator: "\n").write(to: url, atomically: true, encoding: .utf8)
        return url
    }

    private func point(_ text: String, _ importance: Importance = .must, ref: String = "slide 1", topic: String? = nil) -> StudyPoint {
        StudyPoint(text: text, importance: importance, ref: ref, topic: topic)
    }

    func testImportAssignsIncreasingIDs() throws {
        let store = try makeStore()
        let a = try store.importFile(try file("A.pptx", units: 5))
        let b = try store.importFile(try file("B.pdf", units: 2))
        XCTAssertEqual(a.id, "i1")
        XCTAssertEqual(a.name, "A.pptx")
        XCTAssertEqual(a.unitCount, 5)
        XCTAssertEqual(a.chunkCount, 3)
        XCTAssertEqual(a.chunksDone, 0)
        XCTAssertEqual(b.id, "i2")
        XCTAssertEqual(store.imports.map(\.id), ["i1", "i2"])
    }

    func testSameContentReturnsExistingImport() throws {
        let store = try makeStore()
        let a = try store.importFile(try file("A.pptx", units: 3))
        let copy = try store.importFile(try file("Copy of A.pptx", units: 3))
        let other = try store.importFile(try file("A2.pptx", units: 3, salt: "!"))
        XCTAssertEqual(copy, a)
        XCTAssertEqual(other.id, "i2")
        XCTAssertEqual(store.imports.count, 2)
    }

    func testEmptyDocumentIsRejected() throws {
        let store = try StudyStore(directory: storeURL, extract: Self.extract, chunk: { _ in [] })
        XCTAssertThrowsError(try store.importFile(try file("A.pdf", units: 1))) { error in
            XCTAssertEqual(error as? ExtractionError, .empty("A.pdf"))
        }
        XCTAssertTrue(store.imports.isEmpty)
    }

    func testExtractionErrorsPassThrough() throws {
        let store = try StudyStore(directory: storeURL, extract: { throw ExtractionError.unsupportedType($0.lastPathComponent) })
        XCTAssertThrowsError(try store.importFile(try file("A.key", units: 1))) { error in
            XCTAssertEqual(error as? ExtractionError, .unsupportedType("A.key"))
        }
        XCTAssertThrowsError(try store.importFile(root.appendingPathComponent("missing.pdf"))) { error in
            XCTAssertEqual(error as? ExtractionError, .unreadable("missing.pdf"))
        }
    }

    func testChunkTextHasHeader() throws {
        let store = try makeStore()
        _ = try store.importFile(try file("Lecture3.pptx", units: 3))
        XCTAssertEqual(try store.chunk("i1", 1), "Lecture3.pptx · chunk 1/2 · slide 1–slide 2\n\n[slide 1]\nContent 1\n[slide 2]\nContent 2")
        XCTAssertEqual(try store.chunk("i1", 2), "Lecture3.pptx · chunk 2/2 · slide 3\n\n[slide 3]\nContent 3")
    }

    func testChunkAndImportAreChecked() throws {
        let store = try makeStore()
        _ = try store.importFile(try file("A.pdf", units: 3))
        XCTAssertThrowsError(try store.chunk("i1", 0)) { error in
            XCTAssertEqual(error as? StudyError, .chunkOutOfRange(0, chunkCount: 2))
        }
        XCTAssertThrowsError(try store.chunk("i1", 3))
        XCTAssertThrowsError(try store.chunk("i9", 1)) { error in
            XCTAssertEqual(error as? StudyError, .unknownImport("i9"))
        }
        XCTAssertThrowsError(try store.savePoints([], importID: "i1", chunk: 5))
        XCTAssertThrowsError(try store.savePoints([], importID: "i9", chunk: 1))
        XCTAssertThrowsError(try store.points(importID: "i9"))
    }

    func testSavePointsReplacesAndCountsChunks() throws {
        let store = try makeStore()
        _ = try store.importFile(try file("A.pdf", units: 6))
        try store.savePoints([point("Old")], importID: "i1", chunk: 2)
        try store.savePoints([point("New"), point("Detail", .good, ref: "slide 4")], importID: "i1", chunk: 2)
        try store.savePoints([], importID: "i1", chunk: 1)
        XCTAssertEqual(store.imports[0].chunksDone, 2)
        XCTAssertEqual(try store.points(importID: "i1").map(\.text), ["New", "Detail"])
    }

    func testPointsAreTidiedAndRefsNamed() throws {
        let store = try makeStore()
        _ = try store.importFile(try file("A.pdf", units: 2))
        try store.savePoints([
            point("  Two\nlines ", ref: "p. 3", topic: " "),
            point("   "),
            point("Named", ref: "A.pdf · p. 4", topic: "Graphs"),
            point("No ref", ref: ""),
        ], importID: "i1", chunk: 1)
        XCTAssertEqual(try store.points(importID: nil), [
            StudyPoint(text: "Two lines", importance: .must, ref: "A.pdf · p. 3"),
            StudyPoint(text: "Named", importance: .must, ref: "A.pdf · p. 4", topic: "Graphs"),
            StudyPoint(text: "No ref", importance: .must, ref: "A.pdf"),
        ])
    }

    func testPointsFollowDocumentOrder() throws {
        let store = try makeStore()
        _ = try store.importFile(try file("A.pdf", units: 6))
        _ = try store.importFile(try file("B.pdf", units: 2, salt: "b"))
        try store.savePoints([point("B1")], importID: "i2", chunk: 1)
        try store.savePoints([point("A3")], importID: "i1", chunk: 3)
        try store.savePoints([point("A1")], importID: "i1", chunk: 1)
        XCTAssertEqual(try store.points(importID: nil).map(\.text), ["A1", "A3", "B1"])
        XCTAssertEqual(try store.points(importID: "i2").map(\.text), ["B1"])
    }

    func testSummaryFormat() throws {
        let store = try makeStore()
        _ = try store.importFile(try file("Lecture3.pptx", units: 8))
        _ = try store.importFile(try file("Syllabus.pdf", units: 1, salt: "s"))
        try store.savePoints([
            point("TCP is connection-oriented", ref: "slide 2", topic: "transport"),
            point("History of ARPANET", .skip, ref: "slide 1"),
        ], importID: "i1", chunk: 1)
        try store.savePoints([point("Window size", .good, ref: "slide 7")], importID: "i1", chunk: 4)
        XCTAssertEqual(try store.pointsSummary(importID: nil), """
        i1 Lecture3.pptx · chunks 2/4 · not done: 2–3
        ★ TCP is connection-oriented (Lecture3.pptx · slide 2) [transport]
        ✕ History of ARPANET (Lecture3.pptx · slide 1)
        ○ Window size (Lecture3.pptx · slide 7)

        i2 Syllabus.pdf · chunks 0/1 · not done: 1
        (no points)
        """)
        try store.savePoints([point("Essay due 2026-01-15", ref: "slide 1", topic: "task")], importID: "i2", chunk: 1)
        XCTAssertEqual(try store.pointsSummary(importID: "i2"), """
        i2 Syllabus.pdf · chunks 1/1
        ★ Essay due 2026-01-15 (Syllabus.pdf · slide 1) [task]
        """)
    }

    func testSummaryWithoutImports() throws {
        XCTAssertEqual(try makeStore().pointsSummary(importID: nil), "no imports")
    }

    func testRangesAreCompact() {
        XCTAssertEqual(StudyStore.ranges([1, 2, 3, 5, 7, 8]), "1–3, 5, 7–8")
        XCTAssertEqual(StudyStore.ranges([4]), "4")
    }

    func testReopeningKeepsEverything() throws {
        var store: StudyStore? = try makeStore()
        let a = try store!.importFile(try file("A.pdf", units: 4))
        try store!.savePoints([point("Kept", topic: "x")], importID: "i1", chunk: 2)
        store = nil

        let reopened = try makeStore()
        XCTAssertEqual(reopened.imports.count, 1)
        XCTAssertEqual(reopened.imports[0].id, a.id)
        XCTAssertEqual(reopened.imports[0].chunksDone, 1)
        XCTAssertEqual(reopened.imports[0].importedAt.timeIntervalSinceReferenceDate, a.importedAt.timeIntervalSinceReferenceDate, accuracy: 0.001)
        XCTAssertEqual(try reopened.points(importID: "i1").map(\.text), ["Kept"])
        XCTAssertTrue(try reopened.chunk("i1", 2).hasSuffix("[slide 4]\nContent 4"))
        XCTAssertEqual(try reopened.importFile(try file("A.pdf", units: 4)).id, "i1")
    }

    func testRemoveNeverReusesIDs() throws {
        let store = try makeStore()
        _ = try store.importFile(try file("A.pdf", units: 2))
        _ = try store.importFile(try file("B.pdf", units: 2, salt: "b"))
        try store.remove("i1")
        XCTAssertEqual(store.imports.map(\.id), ["i2"])
        XCTAssertFalse(FileManager.default.fileExists(atPath: storeURL.appendingPathComponent("i1").path))
        XCTAssertThrowsError(try store.remove("i1"))

        try store.removeAll()
        XCTAssertTrue(store.imports.isEmpty)
        XCTAssertFalse(FileManager.default.fileExists(atPath: storeURL.appendingPathComponent("i2").path))
        // Removed content can be imported again, under a fresh id.
        XCTAssertEqual(try store.importFile(try file("A.pdf", units: 2)).id, "i3")
        XCTAssertEqual(try makeStore().imports.map(\.id), ["i3"])
    }

    func testCorruptIndexIsAnError() throws {
        try FileManager.default.createDirectory(at: storeURL, withIntermediateDirectories: true)
        try Data("{nope".utf8).write(to: storeURL.appendingPathComponent("index.json"))
        XCTAssertThrowsError(try makeStore()) { error in
            XCTAssertEqual(error as? StudyError, .corrupt("index.json"))
        }
    }

    func testCorruptChunkAndPointsAreErrors() throws {
        let store = try makeStore()
        _ = try store.importFile(try file("A.pdf", units: 4))
        try store.savePoints([point("x")], importID: "i1", chunk: 1)
        try Data("garbage".utf8).write(to: storeURL.appendingPathComponent("i1/chunks/2.json"))
        try Data("[{]".utf8).write(to: storeURL.appendingPathComponent("i1/points/1.json"))
        XCTAssertThrowsError(try store.chunk("i1", 2)) { error in
            XCTAssertEqual(error as? StudyError, .corrupt("i1/chunks/2.json"))
        }
        XCTAssertThrowsError(try store.points(importID: nil)) { error in
            XCTAssertEqual(error as? StudyError, .corrupt("i1/points/1.json"))
        }
        XCTAssertThrowsError(try store.pointsSummary(importID: "i1"))
        XCTAssertNoThrow(try store.chunk("i1", 1))
    }

    func testConcurrentAccess() throws {
        let store = try makeStore()
        _ = try store.importFile(try file("A.pdf", units: 40))
        let failures = NSLock()
        var errors: [Error] = []
        DispatchQueue.concurrentPerform(iterations: 60) { i in
            do {
                let n = i % 20 + 1
                switch i % 3 {
                case 0: try store.savePoints([point("P\(n)"), point("Q\(n)", .good)], importID: "i1", chunk: n)
                case 1: _ = try store.chunk("i1", n)
                default:
                    _ = try store.pointsSummary(importID: nil)
                    _ = try store.importFile(try file("C\(i).pdf", units: 2, salt: "\(i)"))
                }
            } catch {
                failures.lock()
                errors.append(error)
                failures.unlock()
            }
        }
        XCTAssertTrue(errors.isEmpty, "\(errors)")
        let saved = Set((0..<60).filter { $0 % 3 == 0 }.map { $0 % 20 + 1 })
        XCTAssertEqual(store.imports[0].chunksDone, saved.count)
        XCTAssertEqual(try store.points(importID: "i1").count, saved.count * 2)
        XCTAssertEqual(Set(store.imports.map(\.id)).count, 21)
        XCTAssertEqual(try makeStore().imports.count, 21)
    }
}
