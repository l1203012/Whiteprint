import XCTest
@testable import WhiteprintCore

final class DSLParserTests: XCTestCase {
    private func statement(_ line: String) throws -> DSLStatement {
        try DSLParser.statement(from: DSLTokenizer.tokenize(line[...]))
    }

    private func syntaxError(_ line: String) -> String? {
        do {
            _ = try statement(line)
            return nil
        } catch {
            return (error as? DSLSyntaxError)?.message
        }
    }

    // MARK: Tokenizer

    func testTokenizesEveryKind() throws {
        XCTAssertEqual(try DSLTokenizer.tokenize(#"box a -1.5,2 12x4 3 "Say \"hi\"\n" a->b # comment"#), [
            .word("box"), .word("a"), .point(GridPoint(-1.5, 2)), .size(GridSize(12, 4)), .number(3),
            .string("Say \"hi\"\n"), .chain(DSLChain(ids: ["a", "b"], links: [.forward])),
        ])
    }

    func testHashInsideLabelIsNotAComment() throws {
        XCTAssertEqual(try DSLTokenizer.tokenize(##"text "#1""##), [.word("text"), .string("#1")])
    }

    func testLinkOperators() throws {
        let chain = try DSLTokenizer.tokenize("a>b<c<>d-e->f<-g<->h--i")
        XCTAssertEqual(chain, [.chain(DSLChain(ids: ["a", "b", "c", "d", "e", "f", "g", "h", "i"], links: [
            DSLLink(startArrow: false, endArrow: true),
            DSLLink(startArrow: true, endArrow: false),
            DSLLink(startArrow: true, endArrow: true),
            DSLLink(startArrow: false, endArrow: false),
            DSLLink(startArrow: false, endArrow: true),
            DSLLink(startArrow: true, endArrow: false),
            DSLLink(startArrow: true, endArrow: true),
            DSLLink(startArrow: false, endArrow: false),
        ]))])
    }

    func testUnicodeIdentifiers() throws {
        XCTAssertEqual(try DSLTokenizer.tokenize("flow Gebruiker>Dienst_één"), [
            .word("flow"), .chain(DSLChain(ids: ["Gebruiker", "Dienst_één"], links: [.forward])),
        ])
    }

    func testTokenizerErrors() {
        let cases: [(String, String)] = [
            (#"text "open"#, "unterminated \"label\""),
            ("box a 1,2,3", "bad position '1,2,3', use X,Y"),
            ("box a 1,", "bad position '1,', use X,Y"),
            ("box a 4x", "bad size '4x', use WxH"),
            ("box a 0x4", "size '0x4' must be positive"),
            ("arrow a>", "bad link 'a>', use a>b"),
            ("arrow a><b", "bad link 'a><b', use a>b"),
            ("box a 5000,0", "'5000' is too large, max 1000"),
            ("box a$", "unexpected 'a$'"),
            ("box 1e5", "bad size '1e5', use WxH"),
        ]
        for (line, message) in cases {
            XCTAssertThrowsError(try DSLTokenizer.tokenize(line[...]), line) { error in
                XCTAssertEqual((error as? DSLSyntaxError)?.message, message, line)
            }
        }
    }

    // MARK: Statements

    func testShapeArgumentsInAnyOrder() throws {
        let expected = DSLStatement.shape(
            .box, id: "a", at: GridPoint(1, 2), size: GridSize(12, 4), label: "API", style: [.dashed, .thick]
        )
        XCTAssertEqual(try statement(#"box a 1,2 12x4 "API" dashed thick"#), expected)
        XCTAssertEqual(try statement(#"box thick "API" 12x4 a dashed 1,2"#), expected)
    }

    func testCommandAliasesAndCase() throws {
        XCTAssertEqual(try statement("RECT a"), .shape(.box, id: "a", at: nil, size: nil, label: nil, style: []))
        XCTAssertEqual(try statement("oval o 6"), .shape(.circle, id: "o", at: nil, size: GridSize(6, 6), label: nil, style: []))
        XCTAssertEqual(try statement("column a b"), .layout(.col, DSLChain(ids: ["a", "b"], links: [.forward]), gap: nil))
    }

    func testTextIDIsOptional() throws {
        XCTAssertEqual(try statement(#"text 0,0 "Hi" bold"#), .shape(.text, id: nil, at: .zero, size: nil, label: "Hi", style: .bold))
    }

    func testConnectors() throws {
        XCTAssertEqual(try statement(#"arrow a>b "uses""#), .connect(
            DSLChain(ids: ["a", "b"], links: [.forward]), label: "uses", style: []
        ))
        XCTAssertEqual(try statement("arrow a b c"), .connect(
            DSLChain(ids: ["a", "b", "c"], links: [.forward, .forward]), label: nil, style: []
        ))
        XCTAssertEqual(try statement("line a b"), .connect(
            DSLChain(ids: ["a", "b"], links: [DSLLink(startArrow: false, endArrow: false)]), label: nil, style: []
        ))
        XCTAssertEqual(try statement("arrow 0,0 4,0 dashed"), .polyline(
            [.zero, GridPoint(4, 0)], arrow: true, closed: false, label: nil, style: .dashed
        ))
        XCTAssertEqual(try statement("path 0,0 4,0 4,4 closed"), .polyline(
            [.zero, GridPoint(4, 0), GridPoint(4, 4)], arrow: false, closed: true, label: nil, style: []
        ))
    }

    func testDimensionGroupAndLayouts() throws {
        XCTAssertEqual(try statement(#"dim 0,0 10,0 "1 m""#), .dimension(.zero, GridPoint(10, 0), label: "1 m"))
        XCTAssertEqual(try statement(#"group g a b "Backend""#), .group(id: "g", members: ["a", "b"], label: "Backend", style: []))
        XCTAssertEqual(try statement("row a b 2"), .layout(.row, DSLChain(ids: ["a", "b"], links: [.forward]), gap: 2))
        XCTAssertEqual(try statement("flow a<>b"), .layout(
            .flow, DSLChain(ids: ["a", "b"], links: [DSLLink(startArrow: true, endArrow: true)]), gap: nil
        ))
    }

    func testStatementErrors() {
        let cases: [(String, String)] = [
            ("\"x\" box", "expected a command like box, arrow or flow"),
            ("square a", "unknown command 'square'"),
            ("box", "box needs an id, e.g. box a"),
            ("box a b", "unexpected 'b'"),
            ("box a 0,0 1,1", "only one position allowed"),
            ("box a 4", "use WxH for the size, e.g. 12x4"),
            ("circle c 4 4x4", "only one size allowed"),
            (#"box a "x" "y""#, "only one \"label\" allowed"),
            ("box a>b", "box can't link 'a>b', use arrow"),
            ("box a closed", "'closed' only applies to path"),
            ("text 0,0", "text needs a \"label\""),
            (#"text "a" 4x4"#, "text has no size"),
            ("arrow a", "use arrow a>b or arrow X,Y X,Y"),
            ("arrow a>b c", "use arrow a>b or arrow X,Y X,Y"),
            ("arrow a>b 3", "unexpected number '3'"),
            ("path 0,0", "path needs at least 2 points, e.g. path 0,0 4,0 4,4 closed"),
            ("dim 0,0", "dim needs 2 points, e.g. dim 0,0 10,0"),
            ("group g", "group needs an id and members, e.g. group g a b"),
            ("flow", "flow needs ids, e.g. flow a b c"),
            ("row a b>c", "use either 'row a b c' or 'row a>b>c'"),
            ("row a b -1", "gap can't be negative"),
            (#"row a b "x""#, "row takes no \"label\""),
            ("col a b dashed", "col takes no style"),
        ]
        for (line, message) in cases {
            XCTAssertEqual(syntaxError(line), message, line)
        }
    }

    func testParseReportsLineNumbersAndSkipsBlankAndCommentLines() {
        let result = DSLParser.parse("# title\n\nbox a\nsquare b\nbox c")
        XCTAssertEqual(result.lines.map(\.line), [3, 5])
        XCTAssertEqual(result.errors, [DrawingError(line: 4, message: "unknown command 'square'")])
        XCTAssertEqual(result.errors.first?.description, "line 4: unknown command 'square'")
    }

    func testStatementLimit() {
        let source = Array(repeating: "box a", count: DSLParser.maxStatements + 5).joined(separator: "\n")
        let result = DSLParser.parse(source)
        XCTAssertEqual(result.lines.count, DSLParser.maxStatements)
        XCTAssertEqual(result.errors, [DrawingError(line: DSLParser.maxStatements + 1, message: "too many lines, max 500")])
    }
}
