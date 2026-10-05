import XCTest
@testable import WhiteprintCore

final class DrawingCompilerTests: XCTestCase {
    private func compile(_ source: String, file: StaticString = #filePath, line: UInt = #line) -> DrawingScene {
        let result = DrawingCompiler.compile(source)
        XCTAssertEqual(result.errors, [], file: file, line: line)
        return result.scene
    }

    private func assertPoint(
        _ point: GridPoint?, _ x: Double, _ y: Double, file: StaticString = #filePath, line: UInt = #line
    ) {
        guard let point else { return XCTFail("missing point", file: file, line: line) }
        XCTAssertEqual(point.x, x, accuracy: 0.0001, file: file, line: line)
        XCTAssertEqual(point.y, y, accuracy: 0.0001, file: file, line: line)
    }

    func testEmptySource() {
        let result = DrawingCompiler.compile("")
        XCTAssertEqual(result.scene, DrawingScene())
        XCTAssertEqual(result.errors, [])
        XCTAssertEqual(result.scene.bounds, .zero)
    }

    func testExplicitShapesKeepPositionAndSize() {
        let scene = compile(#"box a 2,3 12x6 "API" thick"#)
        XCTAssertEqual(scene.shapes, [SceneShape(
            id: "a", kind: .box, frame: GridRect(x: 2, y: 3, width: 12, height: 6), label: "API", style: .thick
        )])
    }

    func testDefaultSizesGrowToFitLabels() {
        let scene = compile("""
        box a 0,0
        box b 0,10 "Authentication Service"
        box c 0,20 "two\\nlines\\nhere"
        circle d 0,30
        db e 0,40
        text f 0,50 "Hello"
        """)
        XCTAssertEqual(scene.shape("a")?.frame.size, GridSize(10, 4))
        XCTAssertEqual(scene.shape("b")?.frame.size, GridSize(20, 4)) // 22 chars × 0.8 → 18, + 2 padding
        XCTAssertEqual(scene.shape("c")?.frame.size, GridSize(10, 8))
        XCTAssertEqual(scene.shape("d")?.frame.size, GridSize(4, 4))
        XCTAssertEqual(scene.shape("e")?.frame.size, GridSize(8, 5))
        XCTAssertEqual(scene.shape("f")?.frame.size, GridSize(4, 2))
    }

    func testFlowCreatesBoxesPlacesThemInARowAndLinksThem() {
        let scene = compile("flow Client>API>Web_App")
        XCTAssertEqual(scene.shapes.map(\.id), ["Client", "API", "Web_App"])
        XCTAssertEqual(scene.shape("Web_App")?.label, "Web App")
        XCTAssertEqual(scene.shapes.map(\.frame.origin), [GridPoint(0, 0), GridPoint(14, 0), GridPoint(28, 0)])
        XCTAssertEqual(scene.lines.count, 2)
        XCTAssertEqual(scene.lines[0].from, "Client")
        XCTAssertEqual(scene.lines[0].to, "API")
        assertPoint(scene.lines[0].points.first, 10, 2)
        assertPoint(scene.lines[0].points.last, 14, 2)
        XCTAssertTrue(scene.lines[0].endArrow)
        XCTAssertFalse(scene.lines[0].startArrow)
    }

    func testFlowUsesDeclaredShapesEvenWhenDeclaredLater() {
        let scene = compile("""
        flow a>b
        db b "Postgres"
        """)
        XCTAssertEqual(scene.shape("b")?.kind, .db)
        XCTAssertEqual(scene.shape("b")?.frame, GridRect(x: 14, y: -0.5, width: 9, height: 5))
    }

    func testRowCentersVerticallyAndColCentersHorizontally() {
        let scene = compile("""
        box a 0,0
        circle b
        col a c 2
        row a b
        """)
        XCTAssertEqual(scene.shape("b")?.frame.origin, GridPoint(14, 0))
        XCTAssertEqual(scene.shape("c")?.frame.origin, GridPoint(0, 6))
        XCTAssertTrue(scene.lines.isEmpty)
    }

    func testExplicitPositionWinsOverLayout() {
        let scene = compile("""
        box b 50,50
        row a b
        """)
        XCTAssertEqual(scene.shape("b")?.frame.origin, GridPoint(50, 50))
    }

    func testSecondLayoutStartsBelowTheFirst() {
        let scene = compile("""
        flow a>b
        flow c>d
        """)
        XCTAssertEqual(scene.shape("c")?.frame.origin, GridPoint(0, 8))
    }

    func testUnplacedShapesGoInARowBelowEverything() {
        let scene = compile("""
        box a 0,0
        box b
        circle c
        """)
        XCTAssertEqual(scene.shape("b")?.frame.origin, GridPoint(0, 8))
        XCTAssertEqual(scene.shape("c")?.frame.origin, GridPoint(14, 8))
    }

    func testUnnamedTextGetsInternalIDs() {
        let scene = compile("""
        text "one"
        text _t1 "named"
        text "two"
        """)
        XCTAssertEqual(scene.shapes.map(\.id), ["_t2", "_t1", "_t3"])
    }

    func testArrowsClipToRectangleAndEllipseEdges() {
        let scene = compile("""
        box a 0,0 10x4
        circle b 20,0 4
        arrow a>b
        line a<>b "x" dashed
        """)
        assertPoint(scene.lines[0].points[0], 10, 2)
        assertPoint(scene.lines[0].points[1], 20, 2)
        XCTAssertEqual(scene.lines[1].label, "x")
        XCTAssertEqual(scene.lines[1].style, .dashed)
        XCTAssertTrue(scene.lines[1].startArrow && scene.lines[1].endArrow)
    }

    func testDiagonalArrowLeavesThroughTheCorrectSide() {
        let scene = compile("""
        box a 0,0 10x4
        box b 0,20 10x4
        arrow a>b
        """)
        assertPoint(scene.lines[0].points[0], 5, 4)
        assertPoint(scene.lines[0].points[1], 5, 20)
    }

    func testGroupsWrapMembersAndCanBeLinked() {
        let scene = compile("""
        box a 0,0
        box b 14,0
        box c 0,20
        group g a b "Backend"
        arrow c>g
        """)
        let group = scene.group("g")
        XCTAssertEqual(group?.frame, GridRect(x: -1.5, y: -3.5, width: 27, height: 9))
        XCTAssertEqual(group?.style, .dashed)
        XCTAssertEqual(scene.lines.first?.to, "g")
        assertPoint(scene.lines.first?.points.last, 10.5, 5.5)
    }

    func testDimensionsDefaultToTheirLength() {
        let scene = compile("""
        dim 0,0 12,0
        dim 0,0 3,4
        dim 0,0 1,1
        dim 0,0 0,5 "5 m"
        """)
        XCTAssertEqual(scene.dimensions.map(\.label), ["12", "5", "1.4", "5 m"])
        let offset = scene.dimensions[0].offsetLine
        assertPoint(offset.from, 0, -1.5)
        assertPoint(offset.to, 12, -1.5)
    }

    func testBoundsCoverShapesLinesGroupsAndDimensions() {
        let scene = compile("""
        box a 0,0
        line -5,2 3,30
        dim 0,0 10,0
        """)
        XCTAssertEqual(scene.bounds, GridRect(x: -5, y: -1.5, width: 15, height: 31.5))
    }

    func testErrorsAreReportedButTheRestStillDraws() {
        let result = DrawingCompiler.compile("""
        box a
        box a
        arrow a>x
        arrow a>a
        group g a nope
        row g a
        box g
        squiggle
        """)
        XCTAssertEqual(result.errors.map(\.description), [
            "line 2: 'a' is already defined on line 1",
            "line 3: unknown id 'x'",
            "line 4: can't link 'a' to itself",
            "line 5: 'g' is already defined on line 7",
            "line 6: can't lay out group 'g'",
            "line 8: unknown command 'squiggle'",
        ])
        XCTAssertEqual(result.scene.shapes.map(\.id), ["a", "g"])
        XCTAssertTrue(result.scene.lines.isEmpty)
    }

    func testThreeTierExampleFromThePlan() {
        let scene = compile("""
        box c 0,0 "Client"
        box s 12,0 "Server"
        db  d 24,0 "Postgres"
        arrow c>s "HTTPS"
        arrow s>d
        """)
        XCTAssertEqual(scene.shapes.count, 3)
        XCTAssertEqual(scene.lines.map(\.label), ["HTTPS", nil])
    }
}
