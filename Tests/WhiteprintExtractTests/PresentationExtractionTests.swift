import Foundation
import XCTest
@testable import WhiteprintExtract

final class PresentationExtractionTests: XCTestCase {
    private var directory: URL!

    override func setUpWithError() throws {
        directory = try Fixtures.temporaryDirectory()
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: directory)
    }

    private func deck(_ name: String, _ parts: [String: String], stored: Set<String> = []) throws -> URL {
        let url = directory.appendingPathComponent(name)
        try Fixtures.writeZip(parts.merging(["[Content_Types].xml": Fixtures.contentTypes]) { a, _ in a }, stored: stored, to: url)
        return url
    }

    func testReadsTitlesFirstBodyLinesAndSpeakerNotes() throws {
        let url = try deck("Lecture3.pptx", [
            "ppt/slides/slide1.xml": Fixtures.slideXML([
                Fixtures.shape("body", ["Glycolysis splits glucose", "It runs in the |cytoplasm"]),
                Fixtures.shape("title", ["Cell |respiration"]),
                Fixtures.shape("sldNum", ["1"]),
                Fixtures.shape("ftr", ["Biology 101"]),
            ]),
            "ppt/slides/_rels/slide1.xml.rels": Fixtures.notesRels("../notesSlides/notesSlide1.xml"),
            "ppt/notesSlides/notesSlide1.xml": Fixtures.slideXML([
                Fixtures.shape("sldImg", []),
                Fixtures.shape("body", ["Stress that ATP is the &amp; key output.", "Mention the exam."]),
                Fixtures.shape("sldNum", ["1"]),
            ], root: "notes"),
            "ppt/slides/slide2.xml": Fixtures.slideXML([Fixtures.shape(nil, ["A free text box"])]),
        ])

        let document = try DocumentExtractor.extract(url)
        XCTAssertEqual(document.units, [
            ExtractedUnit(ref: "slide 1", text: "Cell respiration\nGlycolysis splits glucose\nIt runs in the cytoplasm\n\nNotes: Stress that ATP is the & key output.\nMention the exam."),
            ExtractedUnit(ref: "slide 2", text: "A free text box"),
        ])
    }

    func testSlidesWithoutPresentationPartAreInNumericOrder() throws {
        let slides = [1, 2, 10].map { ("ppt/slides/slide\($0).xml", Fixtures.slideXML([Fixtures.shape("title", ["Slide file \($0)"])])) }
        let url = try deck("Numeric.pptx", Dictionary(uniqueKeysWithValues: slides))
        let document = try DocumentExtractor.extract(url)
        XCTAssertEqual(document.units.map(\.text), ["Slide file 1", "Slide file 2", "Slide file 10"])
        XCTAssertEqual(document.units.map(\.ref), ["slide 1", "slide 2", "slide 3"])
    }

    func testPresentationPartDefinesSlideOrder() throws {
        var parts = Fixtures.presentationParts(["slide2.xml", "slide1.xml"])
        parts["ppt/slides/slide1.xml"] = Fixtures.slideXML([Fixtures.shape("title", ["Moved to the end"])])
        parts["ppt/slides/slide2.xml"] = Fixtures.slideXML([Fixtures.shape("title", ["Now first"])])
        let url = try deck("Reordered.pptx", parts)
        XCTAssertEqual(try DocumentExtractor.extract(url).units, [
            ExtractedUnit(ref: "slide 1", text: "Now first"),
            ExtractedUnit(ref: "slide 2", text: "Moved to the end"),
        ])
    }

    func testReadsStoredSlideEntries() throws {
        let url = try deck("Stored.pptx", [
            "ppt/slides/slide1.xml": Fixtures.slideXML([Fixtures.shape("title", ["Deflated slide"])]),
            "ppt/slides/slide2.xml": Fixtures.slideXML([Fixtures.shape("title", ["Stored slide"])]),
        ], stored: ["ppt/slides/slide2.xml"])
        XCTAssertEqual(try ZipArchive(url: url).entry(named: "ppt/slides/slide2.xml")?.method, 0)
        XCTAssertEqual(try DocumentExtractor.extract(url).units.map(\.text), ["Deflated slide", "Stored slide"])
    }

    func testRemovesTextRepeatedOnEverySlide() throws {
        var parts: [String: String] = [:]
        for n in 1...4 {
            parts["ppt/slides/slide\(n).xml"] = Fixtures.slideXML([
                Fixtures.shape("title", ["Topic \(["A", "B", "C", "D"][n - 1])"]),
                Fixtures.shape(nil, ["Point for slide \(n)"]),
                Fixtures.shape(nil, ["© 2026 Ghent University"]),
            ])
        }
        let units = try DocumentExtractor.extract(try deck("Footer.pptx", parts)).units
        XCTAssertEqual(units.map(\.text), (1...4).map { "Topic \(["A", "B", "C", "D"][$0 - 1])\nPoint for slide \($0)" })
    }

    func testDeckWithoutTextIsEmpty() throws {
        let url = try deck("Pictures.pptx", ["ppt/slides/slide1.xml": Fixtures.slideXML([Fixtures.shape("sldNum", ["1"])])])
        XCTAssertThrowsError(try DocumentExtractor.extract(url)) {
            XCTAssertEqual($0 as? ExtractionError, .empty("Pictures.pptx"))
        }
    }

    func testZipWithoutSlidesOrMalformedXMLIsUnreadable() throws {
        let noSlides = try deck("NoSlides.pptx", ["docProps/app.xml": "<x/>"])
        XCTAssertThrowsError(try DocumentExtractor.extract(noSlides)) {
            XCTAssertEqual($0 as? ExtractionError, .unreadable("NoSlides.pptx"))
        }
        let malformed = try deck("Malformed.pptx", ["ppt/slides/slide1.xml": "<p:sld><unclosed>"])
        XCTAssertThrowsError(try DocumentExtractor.extract(malformed)) {
            XCTAssertEqual($0 as? ExtractionError, .unreadable("Malformed.pptx"))
        }
        let notZip = directory.appendingPathComponent("Fake.pptx")
        try Data("hello".utf8).write(to: notZip)
        XCTAssertThrowsError(try DocumentExtractor.extract(notZip)) {
            XCTAssertEqual($0 as? ExtractionError, .unreadable("Fake.pptx"))
        }
    }

    func testIgnoresFallbackCopiesOfAlternateContent() throws {
        let xml = """
        <p:sld xmlns:a="\(Fixtures.drawingNS)" xmlns:p="\(Fixtures.presentationNS)" \
        xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"><p:cSld><p:spTree>\
        <mc:AlternateContent><mc:Choice Requires="x">\(Fixtures.shape(nil, ["Once"]))</mc:Choice>\
        <mc:Fallback>\(Fixtures.shape(nil, ["Once"]))</mc:Fallback></mc:AlternateContent>\
        </p:spTree></p:cSld></p:sld>
        """
        XCTAssertEqual(try ShapeTextParser.parse(Data(xml.utf8)).flatMap(\.paragraphs), ["Once"])
    }
}
