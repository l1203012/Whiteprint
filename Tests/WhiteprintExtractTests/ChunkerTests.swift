import Foundation
import XCTest
@testable import WhiteprintExtract

final class ChunkerTests: XCTestCase {
    private func document(_ units: [(String, String)]) -> ExtractedDocument {
        ExtractedDocument(name: "doc.pdf", units: units.map { ExtractedUnit(ref: $0.0, text: $0.1) })
    }

    func testEmptyDocumentHasNoChunks() {
        XCTAssertEqual(Chunker.chunks(document([])), [])
    }

    func testSmallDocumentIsOneChunkWithRefMarkers() {
        let chunks = Chunker.chunks(document([("p. 1", "One"), ("p. 2", "Two\nlines")]))
        XCTAssertEqual(chunks, [ExtractedChunk(index: 1, firstRef: "p. 1", lastRef: "p. 2", text: "[p. 1]\nOne\n\n[p. 2]\nTwo\nlines")])
    }

    func testGroupsUnitsWithoutSplittingThem() {
        // Each block is "[s N]\n" (6) + 20 characters = 26; two blocks with the separator are 54.
        let units = (1...5).map { ("s \($0)", String(repeating: "\($0)", count: 20)) }
        let chunks = Chunker.chunks(document(units), maxCharacters: 54)
        XCTAssertEqual(chunks.map(\.index), [1, 2, 3])
        XCTAssertEqual(chunks.map(\.firstRef), ["s 1", "s 3", "s 5"])
        XCTAssertEqual(chunks.map(\.lastRef), ["s 2", "s 4", "s 5"])
        XCTAssertTrue(chunks.allSatisfy { $0.text.count <= 54 })
        XCTAssertEqual(Chunker.chunks(document(units), maxCharacters: 53).count, 5)
    }

    func testOversizedUnitSplitsAtParagraphsKeepingItsRef() {
        let paragraphs = (1...6).map { String(repeating: Character("\($0)"), count: 30) }
        let chunks = Chunker.chunks(document([("p. 1", "x"), ("p. 2", paragraphs.joined(separator: "\n\n")), ("p. 3", "y")]),
                                    maxCharacters: 80)
        XCTAssertTrue(chunks.allSatisfy { $0.text.count <= 80 }, "\(chunks.map { $0.text.count })")
        let p2Blocks = chunks.flatMap { $0.text.components(separatedBy: "\n\n[") }.filter { $0.contains("p. 2]") }
        XCTAssertEqual(p2Blocks.count, 3)
        XCTAssertTrue(chunks.map(\.text).joined().contains("[p. 2]\n" + paragraphs[0] + "\n\n" + paragraphs[1]))
        XCTAssertEqual(chunks.first?.firstRef, "p. 1")
        XCTAssertEqual(chunks.last?.lastRef, "p. 3")
        for paragraph in paragraphs {
            XCTAssertTrue(chunks.contains { $0.text.contains(paragraph) }, "paragraph lost")
        }
    }

    func testOversizedParagraphSplitsAtLines() {
        let lines = (1...10).map { "line number \($0)" }
        let chunks = Chunker.chunks(document([("slide 1", lines.joined(separator: "\n"))]), maxCharacters: 50)
        XCTAssertGreaterThan(chunks.count, 1)
        XCTAssertTrue(chunks.allSatisfy { $0.text.count <= 50 && $0.text.hasPrefix("[slide 1]\n") })
        let recovered = chunks.flatMap { $0.text.split(separator: "\n").dropFirst() }.map(String.init)
        XCTAssertEqual(recovered, lines)
    }

    func testOneHugeLineIsStillSplit() {
        let text = String(repeating: "word ", count: 100) + String(repeating: "z", count: 120)
        let chunks = Chunker.chunks(document([("p. 9", text)]), maxCharacters: 60)
        XCTAssertTrue(chunks.allSatisfy { $0.text.count <= 60 })
        XCTAssertEqual(chunks.map { $0.text.dropFirst("[p. 9]\n".count) }.joined(separator: " ").filter { $0 == "z" }.count, 120)
    }

    func testDefaultLimitKeepsTypicalDeckInFewChunks() {
        let units = (1...100).map { ("slide \($0)", String(repeating: "content ", count: 60)) }
        let chunks = Chunker.chunks(document(units))
        XCTAssertEqual(chunks.count, 3)
        XCTAssertTrue(chunks.allSatisfy { $0.text.count <= Chunker.defaultMaxCharacters })
        XCTAssertEqual(chunks.map(\.index), [1, 2, 3])
    }
}
