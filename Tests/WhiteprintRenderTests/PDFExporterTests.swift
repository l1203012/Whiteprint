import AppKit
import PDFKit
import XCTest
import WhiteprintCore
@testable import WhiteprintRender

final class PDFExporterTests: XCTestCase {
    private func document(_ note: Note, style: PDFExportStyle = .blueprint) throws -> PDFDocument {
        try XCTUnwrap(PDFDocument(data: PDFExporter.data(for: note, style: style)))
    }

    private func longText(lines: Int) -> String {
        (1...lines).map { "- item \($0) with a few words in it" }.joined(separator: "\n")
    }

    func testPaperSizeFollowsTheRegion() {
        XCTAssertEqual(PDFExporter.paperSize(region: "US"), PDFExporter.letter)
        XCTAssertEqual(PDFExporter.paperSize(region: "CA"), PDFExporter.letter)
        XCTAssertEqual(PDFExporter.paperSize(region: "DE"), PDFExporter.a4)
        XCTAssertEqual(PDFExporter.paperSize(region: nil), PDFExporter.a4)
    }

    func testPagesUseThePaperSize() throws {
        let page = try XCTUnwrap(try document(Note(title: "T")).page(at: 0))
        XCTAssertEqual(page.bounds(for: .mediaBox).size, PDFExporter.paperSize(region: Locale.current.region?.identifier))
    }

    func testEmptyNoteIsOnePage() throws {
        XCTAssertEqual(try document(Note()).pageCount, 1)
    }

    func testEachNotePageStartsAPDFPage() throws {
        let note = Note(title: "Course", pages: [
            NotePage(blocks: [.text("# First\nshort")]),
            NotePage(blocks: [.text("# Second\nshort")]),
        ])
        let pdf = try document(note)
        XCTAssertEqual(pdf.pageCount, 2)
        // PDFKit's extracted text may start with a space.
        let text = { (i: Int) in pdf.page(at: i)?.string?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "" }
        XCTAssertTrue(text(0).hasPrefix("Course"), "title on the first page")
        XCTAssertTrue(text(1).hasPrefix("Second"))
        XCTAssertFalse(pdf.page(at: 1)?.string?.contains("Course") ?? true)
    }

    func testLongPagesFlowOverSeveralPDFPages() throws {
        let note = Note(title: "Long", pages: [
            NotePage(blocks: [.text("intro")]),
            NotePage(blocks: [.text(longText(lines: 150))]),
        ])
        let pdf = try document(note)
        XCTAssertGreaterThanOrEqual(pdf.pageCount, 4)
        let text = (0..<pdf.pageCount).compactMap { pdf.page(at: $0)?.string }.joined(separator: "\n")
        XCTAssertTrue(text.contains("item 1 with"))
        XCTAssertTrue(text.contains("item 150 with"))
    }

    func testDrawingsAndMarkupAreLeftOut() throws {
        let note = Note(title: "T", pages: [NotePage(blocks: [
            .text("Some **bold** words"),
            .drawing(Drawing(id: "d1", source: "flow Zebra>Yak")),
        ])])
        let text = try XCTUnwrap(try document(note).page(at: 0)?.string)
        XCTAssertTrue(text.contains("Some bold words"))
        XCTAssertFalse(text.contains("Zebra"))
        XCTAssertFalse(text.contains("**"))
    }

    func testDrawingOnlyPagesAreSkipped() throws {
        let note = Note(pages: [
            NotePage(blocks: [.text("one")]),
            NotePage(blocks: [.drawing(Drawing(id: "d1", source: "box a"))]),
            NotePage(blocks: [.text("three")]),
        ])
        XCTAssertEqual(try document(note).pageCount, 2)
    }

    func testTitleIsNotRepeatedWhenThePageStartsWithIt() throws {
        let note = Note(title: "Plan", pages: [NotePage(blocks: [.text("# Plan\nbody")])])
        let text = try XCTUnwrap(try document(note).page(at: 0)?.string)
        XCTAssertEqual(text.components(separatedBy: "Plan").count, 2)
    }

    func testStylesPaintTheirPaper() throws {
        for (style, background) in [(PDFExportStyle.blueprint, BlueprintPalette.blueprint.pageBackground), (.print, .white)] {
            let page = try XCTUnwrap(try document(Note(title: "T"), style: style).page(at: 0))
            let bounds = page.bounds(for: .mediaBox)
            let bitmap = Bitmap(width: Int(bounds.width), height: Int(bounds.height))
            bitmap.withGraphicsContext {
                bitmap.context.saveGState()
                bitmap.context.translateBy(x: 0, y: bounds.height)
                bitmap.context.scaleBy(x: 1, y: -1)
                page.draw(with: .mediaBox, to: bitmap.context)
                bitmap.context.restoreGState()
            }
            XCTAssertTrue(bitmap.pixel(5, 5, matches: background, tolerance: 16), "\(style) paper")
            XCTAssertTrue(bitmap.pixel(Int(bounds.width) - 5, Int(bounds.height) - 5, matches: background, tolerance: 16))
        }
    }
}

final class PageThumbnailTests: XCTestCase {
    private let page = NotePage(blocks: [
        .text("# Overview\nSome **notes**.\n- [ ] todo"),
        .drawing(Drawing(id: "d1", source: "flow Client>API>DB")),
        .text("More text."),
    ])

    private func render(_ page: NotePage, size: CGSize, palette: BlueprintPalette = .blueprint) -> Bitmap {
        let image = PageThumbnail.image(for: page, size: size, palette: palette)
        let bitmap = Bitmap(width: Int(size.width), height: Int(size.height))
        bitmap.withGraphicsContext {
            image.draw(in: CGRect(origin: .zero, size: size))
        }
        return bitmap
    }

    func testImageHasTheRequestedSize() {
        XCTAssertEqual(PageThumbnail.image(for: page, size: CGSize(width: 90, height: 120)).size, CGSize(width: 90, height: 120))
    }

    func testDrawsBackgroundAndContent() {
        let size = CGSize(width: 160, height: 220)
        let palette = BlueprintPalette.blueprint
        let thumbnail = render(page, size: size)
        let empty = render(NotePage(), size: size)
        XCTAssertTrue(thumbnail.pixel(1, 218, matches: palette.pageBackground, tolerance: 20))
        XCTAssertEqual(empty.count(matching: palette.text, tolerance: 60), 0)
        XCTAssertGreaterThan(thumbnail.count(matching: palette.text, tolerance: 60), 100)
    }

    func testTinyThumbnailsStillShowSomething() {
        let thumbnail = render(page, size: CGSize(width: 40, height: 56))
        let background = BlueprintPalette.blueprint.pageBackground
        let ink = (0..<56).flatMap { y in (0..<40).map { (x: $0, y: y) } }
            .filter { !thumbnail.pixel($0.x, $0.y, matches: background, tolerance: 25) }
        XCTAssertGreaterThan(ink.count, 10)
    }

    func testPrintPalette() {
        let thumbnail = render(page, size: CGSize(width: 120, height: 160), palette: .print)
        XCTAssertTrue(thumbnail.pixel(2, 158, matches: .white, tolerance: 10))
    }

    func testPlainTextDropsHeadingMarkerAndInlineMarkup() {
        XCTAssertEqual(PageThumbnail.plainText("## A **b** `c` [d](e)", markerLength: 3), "A b c d")
    }
}
