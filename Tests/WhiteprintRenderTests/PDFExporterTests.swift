import AppKit
import PDFKit
import XCTest
import WhiteprintCore
@testable import WhiteprintRender

final class PDFExporterTests: XCTestCase {
    private let date = Date(timeIntervalSince1970: 1_790_000_000)

    private func document(_ note: Note, style: PDFExportStyle = .blueprint) throws -> PDFDocument {
        try XCTUnwrap(PDFDocument(data: PDFExporter.data(for: note, style: style, date: date)))
    }

    private func render(_ page: PDFPage) -> Bitmap {
        let bounds = page.bounds(for: .mediaBox)
        let bitmap = Bitmap(width: Int(bounds.width), height: Int(bounds.height))
        bitmap.withGraphicsContext {
            bitmap.context.saveGState()
            bitmap.context.translateBy(x: 0, y: bounds.height)
            bitmap.context.scaleBy(x: 1, y: -1)
            page.draw(with: .mediaBox, to: bitmap.context)
            bitmap.context.restoreGState()
        }
        return bitmap
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
        // Print pages, since blueprint pages repeat the title in their title block.
        let pdf = try document(note, style: .print)
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
        let text = try XCTUnwrap(try document(note, style: .print).page(at: 0)?.string)
        XCTAssertEqual(text.components(separatedBy: "Plan").count, 2)
    }

    func testStylesPaintTheirPaper() throws {
        let blueprint = render(try XCTUnwrap(try document(Note(title: "T")).page(at: 0)))
        let print = render(try XCTUnwrap(try document(Note(title: "T"), style: .print).page(at: 0)))
        let middle = blueprint.height / 2
        XCTAssertTrue(blueprint.pixel(10, middle, matches: BlueprintPalette.blueprint.pageBackground, tolerance: 16))
        let corner = blueprint.pixel(blueprint.width - 3, blueprint.height - 3)
        XCTAssertGreaterThan(corner.b, corner.r + 40, "vignetted corners stay blue")
        XCTAssertTrue(print.pixel(5, 5, matches: .white, tolerance: 4))
        XCTAssertTrue(print.pixel(print.width - 5, print.height - 5, matches: .white, tolerance: 4))
    }

    func testBlueprintPagesHaveATitleBlockAndPrintPagesANumber() throws {
        let note = Note(title: "Survey", pages: [NotePage(blocks: [.text("one")]), NotePage(blocks: [.text("two")])])
        let blueprint = try document(note)
        // PDFKit guesses word spaces from the gaps between glyphs, and the guess
        // varies by macOS version: "2 / 2" and the letter-spaced "WHITEPRINT"
        // come out with or without spaces. Compare the text without whitespace.
        let last = try XCTUnwrap(blueprint.page(at: 1)?.string).filter { !$0.isWhitespace }
        XCTAssertTrue(last.contains("Survey"), last)
        XCTAssertTrue(last.contains("2/2"), "page n / N: \(last)")
        XCTAssertTrue(last.contains("2026-09-21"), last)
        XCTAssertTrue(last.contains("WHITEPRINT"), last)
        let print = try XCTUnwrap(try document(note, style: .print).page(at: 1)?.string).filter { !$0.isWhitespace }
        XCTAssertFalse(print.contains("WHITEPRINT"), print)
        XCTAssertTrue(print.hasSuffix("2"), print)
    }

    /// Viewers split copied text into fragments when hundreds of stroked
    /// lines sit under it, which the grid pattern avoids.
    func testBodyTextCopiesOutWhole() throws {
        let sentence = "The deck carries a uniform load of four kilonewtons per metre over the full span"
        let note = Note(title: "Copy", pages: [NotePage(blocks: [.text("\(sentence).\n\n" + longText(lines: 10))])])
        let page = try XCTUnwrap(try document(note).page(at: 0))
        let text = try XCTUnwrap(page.string)
        XCTAssertTrue(text.trimmingCharacters(in: .whitespacesAndNewlines).hasPrefix("Copy"), "page text comes before the title block")
        let selected = try XCTUnwrap(page.selection(for: page.bounds(for: .mediaBox))?.string)
        XCTAssertTrue(selected.replacingOccurrences(of: "\n", with: " ").contains(sentence))
        for line in 1...10 {
            XCTAssertTrue(selected.contains("item \(line) with a few words in it"), "item \(line)")
        }
    }

    func testBlueprintPagesStaySmall() throws {
        let note = { (pages: Int) in
            Note(title: "Size", pages: (1...pages).map { NotePage(blocks: [.text("# Page \($0)\nsome words")]) })
        }
        let one = PDFExporter.data(for: note(1), style: .blueprint, date: date).count
        let ten = PDFExporter.data(for: note(10), style: .blueprint, date: date).count
        XCTAssertLessThan(one, 60_000)
        XCTAssertLessThan((ten - one) / 9, 6_000, "bytes per extra page")
    }
}

final class BlueprintBackgroundTests: XCTestCase {
    private let size = CGSize(width: 200, height: 200)

    private func render(_ options: BlueprintBackground.Options, palette: BlueprintPalette = .blueprint) -> Bitmap {
        let bitmap = Bitmap(width: Int(size.width), height: Int(size.height))
        bitmap.withGraphicsContext {
            BlueprintBackground.draw(in: bitmap.context, rect: CGRect(origin: .zero, size: size), palette: palette, options: options)
        }
        return bitmap
    }

    private func brightness(_ pixel: (r: Int, g: Int, b: Int, a: Int)) -> Int {
        pixel.r + pixel.g + pixel.b
    }

    func testPlainIsThePageColour() {
        let bitmap = render(.plain)
        let background = BlueprintPalette.blueprint.pageBackground
        XCTAssertEqual(bitmap.count(matching: background, tolerance: 1), Int(size.width * size.height))
    }

    func testGridHasMinorAndMajorLines() {
        let bitmap = render(.init(gradient: false, texture: false))
        let paper = brightness(bitmap.pixel(55, 55))
        let minor = brightness(bitmap.pixel(60, 55))
        let major = brightness(bitmap.pixel(50, 55))
        XCTAssertGreaterThan(minor, paper)
        XCTAssertGreaterThan(major, minor)
        XCTAssertEqual(brightness(bitmap.pixel(150, 155)), major, "every 50 pt")
    }

    func testTextureIsDeterministicAndFaint() throws {
        XCTAssertEqual(try XCTUnwrap(PaperTexture.tile).width, PaperTexture.pixels)
        XCTAssertEqual(PaperTexture.coverage(seed: 7, size: 64), PaperTexture.coverage(seed: 7, size: 64))
        XCTAssertNotEqual(PaperTexture.coverage(seed: 7, size: 64), PaperTexture.coverage(seed: 8, size: 64))

        let bitmap = render(.init(gradient: false, grid: false))
        let background = BlueprintPalette.blueprint.pageBackground
        let total = Int(size.width * size.height)
        XCTAssertLessThan(bitmap.count(matching: background, tolerance: 1), total, "some texture")
        XCTAssertEqual(bitmap.count(matching: background, tolerance: 40), total, "but faint")
    }

    func testTitleBlockSitsInTheBottomRightCorner() {
        let sheet = CGSize(width: 400, height: 300)
        let bitmap = Bitmap(width: Int(sheet.width), height: Int(sheet.height))
        let block = BlueprintBackground.TitleBlock(title: "Survey", page: 1, pageCount: 3, date: Date())
        bitmap.withGraphicsContext {
            BlueprintBackground.draw(in: bitmap.context, rect: CGRect(origin: .zero, size: sheet), palette: .blueprint,
                                     options: .init(gradient: false, texture: false, grid: false, titleBlock: block))
        }
        let blockRect = CGRect(
            x: sheet.width - BlueprintBackground.frameInset - BlueprintBackground.titleBlockSize.width,
            y: sheet.height - BlueprintBackground.frameInset - BlueprintBackground.titleBlockSize.height,
            width: BlueprintBackground.titleBlockSize.width, height: BlueprintBackground.titleBlockSize.height
        ).insetBy(dx: 2, dy: 2)
        var ink = 0
        for y in Int(blockRect.minY)..<Int(blockRect.maxY) {
            for x in Int(blockRect.minX)..<Int(blockRect.maxX) where bitmap.pixel(x, y, matches: .white, tolerance: 60) {
                ink += 1
            }
        }
        XCTAssertGreaterThan(ink, 100)
        XCTAssertTrue(bitmap.pixel(Int(blockRect.minX) - 20, Int(blockRect.midY), matches: BlueprintPalette.blueprint.pageBackground, tolerance: 2))
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
