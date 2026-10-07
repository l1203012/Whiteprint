import Foundation
import XCTest
@testable import WhiteprintExtract

final class PDFExtractionTests: XCTestCase {
    private var directory: URL!

    override func setUpWithError() throws {
        directory = try Fixtures.temporaryDirectory()
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: directory)
    }

    private func textPage(_ number: Int, _ body: [String]) -> Fixtures.PDFPage {
        .text(["Biology 101 - Lecture notes", ""] + body + ["", "Ghent University", "\(number)"])
    }

    func testExtractsPagesAndRemovesHeadersFootersAndPageNumbers() throws {
        let url = directory.appendingPathComponent("Syllabus.pdf")
        try Fixtures.writePDF([
            textPage(1, ["Cells are the basic unit of life.", "Each cell stores infor-", "mation in DNA."]),
            textPage(2, ["Mitochondria produce energy for the cell."]),
            .blank,
            textPage(4, ["Ribosomes build proteins from amino acids."]),
        ], to: url)

        let document = try DocumentExtractor.extract(url, ocr: false)
        XCTAssertEqual(document.name, "Syllabus.pdf")
        // Whether two lines of one paragraph come out of PDFKit joined by a space
        // or a line break varies by macOS version (15 keeps breaks that 13 and 14
        // join). Extraction keeps line breaks as given, so compare them as spaces.
        let units = document.units.map { ExtractedUnit(ref: $0.ref, text: $0.text.replacingOccurrences(of: "\n", with: " ")) }
        XCTAssertEqual(units, [
            ExtractedUnit(ref: "p. 1", text: "Cells are the basic unit of life. Each cell stores information in DNA."),
            ExtractedUnit(ref: "p. 2", text: "Mitochondria produce energy for the cell."),
            ExtractedUnit(ref: "p. 4", text: "Ribosomes build proteins from amino acids."),
        ])
    }

    func testOCRsPagesWithoutATextLayer() throws {
        let url = directory.appendingPathComponent("Scan.pdf")
        try Fixtures.writePDF([
            .text(["A normal page with a text layer."]),
            .image(["Photosynthesis converts light", "into chemical energy"]),
        ], to: url)

        let document = try DocumentExtractor.extract(url)
        XCTAssertEqual(document.units.map(\.ref), ["p. 1", "p. 2"])
        let scanned = document.units[1].text.lowercased()
        XCTAssertTrue(scanned.contains("photosynthesis"), scanned)
        XCTAssertTrue(scanned.contains("chemical energy"), scanned)
        XCTAssertLessThan(scanned.range(of: "photosynthesis")!.lowerBound, scanned.range(of: "chemical")!.lowerBound)

        let withoutOCR = try DocumentExtractor.extract(url, ocr: false)
        XCTAssertEqual(withoutOCR.units.map(\.ref), ["p. 1"])
    }

    func testOCRLanguagesAreSupportedOnes() {
        XCTAssertTrue(TextRecognizer.languages.contains("en-US"))
        XCTAssertTrue(Set(TextRecognizer.languages).isSubset(of: Set(TextRecognizer.preferredLanguages)))
    }

    func testBlankPDFIsEmpty() throws {
        let url = directory.appendingPathComponent("Blank.pdf")
        try Fixtures.writePDF([.blank, .blank], to: url)
        XCTAssertThrowsError(try DocumentExtractor.extract(url)) {
            XCTAssertEqual($0 as? ExtractionError, .empty("Blank.pdf"))
        }
    }

    func testCorruptPDFIsUnreadable() throws {
        let url = directory.appendingPathComponent("Broken.pdf")
        try Data("%PDF-1.4 this is not really a pdf".utf8).write(to: url)
        XCTAssertThrowsError(try DocumentExtractor.extract(url)) {
            XCTAssertEqual($0 as? ExtractionError, .unreadable("Broken.pdf"))
        }
    }

    func testLargePDFExtractsEveryPage() throws {
        let url = directory.appendingPathComponent("Reader.pdf")
        let pages = (1...300).map { n in
            textPage(n, (1...30).map { "Page \(n) line \($0): the quick brown fox jumps over the lazy dog." })
        }
        try Fixtures.writePDF(pages, to: url)

        let start = Date()
        let document = try DocumentExtractor.extract(url)
        let seconds = Date().timeIntervalSince(start)
        XCTAssertEqual(document.units.count, 300)
        XCTAssertEqual(document.units.last?.ref, "p. 300")
        XCTAssertFalse(document.units.contains { $0.text.contains("Ghent University") })

        let chunks = Chunker.chunks(document)
        XCTAssertEqual(chunks.first?.firstRef, "p. 1")
        XCTAssertEqual(chunks.last?.lastRef, "p. 300")
        print(String(format: "  300-page PDF: %.2f s, %d chunks, peak RSS %.0f MB", seconds, chunks.count, peakResidentMegabytes()))
    }

    private func peakResidentMegabytes() -> Double {
        var usage = rusage()
        getrusage(RUSAGE_SELF, &usage)
        return Double(usage.ru_maxrss) / 1_048_576
    }
}
