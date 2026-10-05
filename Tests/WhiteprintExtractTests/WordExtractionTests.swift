import AppKit
import XCTest
@testable import WhiteprintExtract

final class WordExtractionTests: XCTestCase {
    private var directory: URL!

    override func setUpWithError() throws {
        directory = try Fixtures.temporaryDirectory()
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: directory)
    }

    private let intro = "Course overview and grading. The exam counts for seventy percent of the final grade and the project for thirty percent."

    func testSplitsDOCXIntoSectionsAtHeadings() throws {
        let url = directory.appendingPathComponent("Course.docx")
        try Fixtures.writeWord([
            Fixtures.body(intro),
            Fixtures.heading("Chapter 1"),
            Fixtures.heading("1.1 The cell"),
            Fixtures.body("Cells are the basic unit of life."),
            Fixtures.body("They divide by mitosis."),
            Fixtures.heading("Chapter 2: A long heading about the many shapes and structures of proteins"),
            Fixtures.body("Proteins fold into shapes."),
        ], type: .officeOpenXML, to: url)

        let document = try DocumentExtractor.extract(url)
        XCTAssertEqual(document.units.map(\.ref), [
            "section 1",
            "§ Chapter 1",
            "§ Chapter 2: A long heading about the many shapes and structu…",
        ])
        XCTAssertEqual(document.units[1].text, "Chapter 1\n1.1 The cell\nCells are the basic unit of life.\nThey divide by mitosis.")
        XCTAssertTrue(document.units[2].text.hasSuffix("\nProteins fold into shapes."))
    }

    func testShortLineBeforeLongBodyCountsAsHeading() {
        let long = String(repeating: "Body text that explains the topic in detail. ", count: 5)
        let string = NSMutableAttributedString()
        [Fixtures.body("Introduction"), Fixtures.body(long), Fixtures.body("- a list item"), Fixtures.body("short")].forEach(string.append)
        XCTAssertEqual(RichTextExtractor.paragraphs(string).map(\.isHeading), [true, false, false, false])
    }

    func testDocumentWithoutHeadingsUsesNumberedSections() throws {
        let url = directory.appendingPathComponent("Notes.docx")
        let paragraph = String(repeating: "Plain text without any headings at all, just sentences. ", count: 20)
        try Fixtures.writeWord((1...10).map { _ in Fixtures.body(paragraph) }, type: .officeOpenXML, to: url)

        let document = try DocumentExtractor.extract(url)
        XCTAssertGreaterThan(document.units.count, 1)
        XCTAssertEqual(document.units.map(\.ref), document.units.indices.map { "section \($0 + 1)" })
        XCTAssertTrue(document.units.allSatisfy { $0.text.count <= RichTextExtractor.maxSectionLength })
    }

    func testLongSectionIsSplitIntoParts() {
        let paragraphs = [RichTextExtractor.Paragraph(text: "Methods", isHeading: true)]
            + (1...5).map { RichTextExtractor.Paragraph(text: String(repeating: "\($0)", count: 2_500), isHeading: false) }
        XCTAssertEqual(RichTextExtractor.sections(paragraphs).map(\.ref), ["§ Methods", "§ Methods, part 2", "§ Methods, part 3"])
    }

    func testReadsLegacyDOC() throws {
        let url = directory.appendingPathComponent("Old.doc")
        try Fixtures.writeWord([Fixtures.heading("Summary"), Fixtures.body("Legacy Word files still work.")], type: .docFormat, to: url)
        let document = try DocumentExtractor.extract(url)
        XCTAssertEqual(document.units, [ExtractedUnit(ref: "§ Summary", text: "Summary\nLegacy Word files still work.")])
    }

    func testCorruptDOCXIsUnreadable() throws {
        let url = directory.appendingPathComponent("Broken.docx")
        try Data("PK not really a docx".utf8).write(to: url)
        XCTAssertThrowsError(try DocumentExtractor.extract(url)) {
            XCTAssertEqual($0 as? ExtractionError, .unreadable("Broken.docx"))
        }
    }
}
