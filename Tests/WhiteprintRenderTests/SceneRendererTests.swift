import AppKit
import XCTest
import WhiteprintCore
@testable import WhiteprintRender

final class SceneRendererTests: XCTestCase {
    private let palette = BlueprintPalette.blueprint

    private func scene(_ source: String, file: StaticString = #filePath, line: UInt = #line) -> DrawingScene {
        let compiled = DrawingCompiler.compile(source)
        XCTAssertEqual(compiled.errors, [], file: file, line: line)
        return compiled.scene
    }

    /// Renders `source` on the page background, canvas-sized.
    private func render(_ source: String) -> Bitmap {
        let scene = scene(source)
        let size = SceneRenderer.canvasSize(for: scene)
        let bitmap = Bitmap(width: Int(size.width), height: Int(size.height), background: palette.pageBackground)
        SceneRenderer.draw(scene, in: bitmap.context, palette: palette)
        return bitmap
    }

    // MARK: Canvas size

    func testEmptySceneIsJustTheMargins() {
        XCTAssertEqual(SceneRenderer.canvasSize(for: DrawingScene()), CGSize(width: 20, height: 20))
    }

    func testCanvasIsBoundsInPointsPlusMargins() {
        XCTAssertEqual(SceneRenderer.canvasSize(for: scene("box a 0,0 10x4")), CGSize(width: 120, height: 60))
    }

    func testCanvasIgnoresWhereTheBoundsStart() {
        XCTAssertEqual(SceneRenderer.canvasSize(for: scene("box a 5,7 10x4")), CGSize(width: 120, height: 60))
    }

    func testCanvasIncludesGroupFrames() {
        // 10x4 box grown by 1.5 units of group padding on every side.
        XCTAssertEqual(SceneRenderer.canvasSize(for: scene("box a 0,0 10x4\ngroup g a")), CGSize(width: 150, height: 90))
    }

    // MARK: Pixels

    func testBoxStrokeIsTextColourAndInsideIsBackground() {
        let bitmap = render("box a 0,0 10x4")
        XCTAssertTrue(bitmap.pixel(10, 30, matches: palette.text, tolerance: 60), "left edge")
        XCTAssertTrue(bitmap.pixel(60, 10, matches: palette.text, tolerance: 60), "top edge")
        XCTAssertTrue(bitmap.pixel(30, 25, matches: palette.pageBackground))
        XCTAssertTrue(bitmap.pixel(3, 3, matches: palette.pageBackground), "margin")
    }

    func testOffsetSceneStartsAtTheMargin() {
        let bitmap = render("box a 5,7 10x4")
        XCTAssertTrue(bitmap.pixel(10, 30, matches: palette.text, tolerance: 60))
    }

    func testCircleAndCylinder() {
        let circle = render("circle c 0,0 4")
        XCTAssertTrue(circle.pixel(10, 30, matches: palette.text, tolerance: 60))
        XCTAssertTrue(circle.pixel(30, 30, matches: palette.pageBackground))
        XCTAssertTrue(circle.pixel(13, 13, matches: palette.pageBackground), "corner outside the ellipse")

        let db = render("db d 0,0 8x5")
        XCTAssertTrue(db.pixel(50, 10, matches: palette.text, tolerance: 60), "top of the cap")
        XCTAssertTrue(db.pixel(10, 40, matches: palette.text, tolerance: 60), "side")
        XCTAssertTrue(db.pixel(50, 60, matches: palette.text, tolerance: 60), "bottom arc")
        XCTAssertTrue(db.pixel(50, 40, matches: palette.pageBackground), "body")
    }

    func testArrowheadIsFilled() {
        let arrow = render("arrow 0,0 10,0")
        let line = render("line 0,0 10,0")
        XCTAssertGreaterThan(arrow.pixel(104, 11).r, 0xC0, "inside the head")
        XCTAssertTrue(line.pixel(104, 12, matches: palette.pageBackground))
        XCTAssertTrue(line.pixel(104, 10, matches: palette.text, tolerance: 60))
    }

    func testDashedLineHasGaps() {
        let dashed = render("line 0,0 20,0 dashed")
        let solid = render("line 0,0 20,0")
        let gaps = { (bitmap: Bitmap) in (12..<208).filter { bitmap.pixel($0, 10, matches: self.palette.pageBackground, tolerance: 30) }.count }
        XCTAssertGreaterThan(gaps(dashed), 40)
        XCTAssertEqual(gaps(solid), 0)
    }

    func testThickLinesAreWider() {
        // Total ink across a column, so partial coverage counts too.
        let width = { (source: String) -> Int in
            let bitmap = self.render(source)
            return (0..<bitmap.height).reduce(0) { $0 + bitmap.pixel(60, $1).r }
        }
        XCTAssertGreaterThan(width("line 0,0 10,0 thick"), width("line 0,0 10,0"))
    }

    func testLineLabelKnocksOutTheLine() {
        let bitmap = render(#"line 0,0 20,0 " ""#)
        XCTAssertTrue(bitmap.pixel(110, 10, matches: palette.pageBackground))
        XCTAssertTrue(bitmap.pixel(40, 10, matches: palette.text, tolerance: 60))
    }

    func testDimensionDrawsOffsetLineAndExtensionLines() {
        // Measured from (0,4) to (10,4); the offset line is 1.5 units above.
        let bitmap = render("dim 0,4 10,4")
        XCTAssertGreaterThan(max(bitmap.pixel(30, 9).r, bitmap.pixel(30, 10).r), 0x70, "offset line")
        XCTAssertTrue(bitmap.pixel(10, 20, matches: palette.text, tolerance: 120), "extension line")
        XCTAssertTrue(bitmap.pixel(30, 25, matches: palette.pageBackground), "nothing on the measured segment")
    }

    func testGroupFrameUsesMutedColour() {
        let bitmap = render("box a 0,0 10x4\ngroup g a")
        let column = (0..<bitmap.height).map { bitmap.pixel(10, $0) }
        XCTAssertTrue(column.contains { $0.r > 0x60 && $0.r < 0xF0 }, "muted stroke on the left edge")
    }

    func testLabelsDrawText() {
        let blank = render("box a 0,0 10x4")
        let labelled = render(#"box a 0,0 10x4 "Label""#)
        XCTAssertGreaterThan(labelled.count(matching: palette.text), blank.count(matching: palette.text) + 20)
    }

    // MARK: Geometry

    func testMidpointIsHalfwayAlongThePolyline() {
        let mid = ScenePainter.midpoint(of: [CGPoint(x: 0, y: 0), CGPoint(x: 10, y: 0), CGPoint(x: 10, y: 30)])
        XCTAssertEqual(mid.x, 10, accuracy: 0.001)
        XCTAssertEqual(mid.y, 10, accuracy: 0.001)
    }

    func testArrowShaftStopsUnderTheHead() {
        let head = ArrowHead(thick: false)
        let base = head.base(tip: CGPoint(x: 100, y: 0), from: .zero)
        XCTAssertEqual(base.x, 100 - head.length + 1, accuracy: 0.001)
        XCTAssertEqual(head.base(tip: CGPoint(x: 5, y: 0), from: .zero), CGPoint(x: 5, y: 0), "too short to shorten")
    }
}

final class DrawingViewTests: XCTestCase {
    func testIntrinsicSizeIsTheCanvas() {
        let view = DrawingView(source: "box a 0,0 10x4")
        XCTAssertEqual(view.intrinsicContentSize, NSSize(width: 120, height: 60))
        XCTAssertTrue(view.isFlipped)
        XCTAssertFalse(view.isOpaque)
    }

    func testEmptyDrawingKeepsAClickableHeight() {
        let view = DrawingView(source: "")
        XCTAssertEqual(view.intrinsicContentSize.height, DrawingView.minimumHeight)
    }

    func testRecompilesWhenSourceChanges() {
        let view = DrawingView(source: "arrow a>b")
        XCTAssertEqual(view.errors.map(\.line), [1])
        view.source = "flow a>b"
        XCTAssertEqual(view.errors, [])
        XCTAssertEqual(view.scene.shapes.count, 2)
        XCTAssertEqual(view.intrinsicContentSize, SceneRenderer.canvasSize(for: view.scene))
    }

    func testDrawsOnATransparentBackground() throws {
        let view = DrawingView(source: "box a 0,0 10x4")
        view.frame = NSRect(origin: .zero, size: view.intrinsicContentSize)
        let rep = try XCTUnwrap(view.bitmapImageRepForCachingDisplay(in: view.bounds))
        view.cacheDisplay(in: view.bounds, to: rep)
        let scale = CGFloat(rep.pixelsWide) / view.bounds.width
        let edge = try XCTUnwrap(rep.colorAt(x: Int(10 * scale), y: Int(30 * scale)))
        XCTAssertGreaterThan(edge.alphaComponent, 0.5)
        let inside = try XCTUnwrap(rep.colorAt(x: Int(30 * scale), y: Int(25 * scale)))
        XCTAssertEqual(inside.alphaComponent, 0, accuracy: 0.01)
    }
}
