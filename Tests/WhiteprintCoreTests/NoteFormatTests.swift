import XCTest
@testable import WhiteprintCore

final class NoteFormatTests: XCTestCase {
    func testParsesFrontMatterPagesAndDrawings() throws {
        let note = try Note(parsing: """
        ---
        whiteprint: 1
        title: Network sketch
        ---
        # Overview
        Some notes.

        ```wp id=d1
        box a 0,0 "Client"
        ```

        +++page
        # Page 2
        """)

        XCTAssertEqual(note.frontMatter.title, "Network sketch")
        XCTAssertEqual(note.frontMatter.formatVersion, 1)
        XCTAssertEqual(note.pages, [
            NotePage(blocks: [
                .text("# Overview\nSome notes."),
                .drawing(Drawing(id: "d1", source: "box a 0,0 \"Client\"")),
            ]),
            NotePage(blocks: [.text("# Page 2")]),
        ])
    }

    func testFileWithoutFrontMatterIsOneMarkdownPage() throws {
        let note = try Note(parsing: "Just text\n")
        XCTAssertEqual(note.frontMatter.formatVersion, Note.formatVersion)
        XCTAssertNil(note.frontMatter.title)
        XCTAssertEqual(note.pages, [NotePage(blocks: [.text("Just text")])])
    }

    func testEmptyFileHasOneEmptyPage() throws {
        XCTAssertEqual(try Note(parsing: "").pages, [NotePage()])
    }

    func testCRLFLineEndingsAreNormalized() throws {
        let note = try Note(parsing: "---\r\ntitle: A\r\n---\r\nOne\r\n+++page\r\nTwo\r\n")
        XCTAssertEqual(note.frontMatter.title, "A")
        XCTAssertEqual(note.pages.map(\.blocks), [[.text("One")], [.text("Two")]])
    }

    func testFrontMatterKeepsUnknownFieldsInOrderAndSplitsAtFirstColon() throws {
        let note = try Note(parsing: "---\ntags: a, b\ntitle: Time: 10:00\n---\n")
        XCTAssertEqual(note.frontMatter.fields.map(\.key), ["whiteprint", "tags", "title"])
        XCTAssertEqual(note.frontMatter.title, "Time: 10:00")
        XCTAssertEqual(note.frontMatter["tags"], "a, b")
    }

    func testNewerVersionIsRejected() {
        XCTAssertThrowsError(try Note(parsing: "---\nwhiteprint: 2\n---\n")) { error in
            XCTAssertEqual(error as? NoteFormatError, .unsupportedVersion(2))
        }
    }

    func testNonNumericVersionIsRejected() {
        XCTAssertThrowsError(try Note(parsing: "---\nwhiteprint: one\n---\n")) { error in
            XCTAssertEqual(error as? NoteFormatError, .invalidVersion("one"))
        }
    }

    func testStructureInsideOtherCodeFencesIsPlainText() throws {
        let text = """
        ````markdown
        +++page
        ```wp
        box a
        ```
        ````
        """
        let note = try Note(parsing: text)
        XCTAssertEqual(note.pages, [NotePage(blocks: [.text(text)])])
    }

    func testTildeFenceCanHoldBacktickLines() throws {
        let text = "~~~\n```wp\n+++page\n~~~"
        XCTAssertEqual(try Note(parsing: text).pages, [NotePage(blocks: [.text(text)])])
    }

    func testFourSpaceIndentedFenceIsNotAFence() throws {
        let note = try Note(parsing: "    ```wp\nbox a\n+++page\nnext")
        XCTAssertEqual(note.pages.count, 2)
        XCTAssertTrue(note.drawings.isEmpty)
    }

    func testUnterminatedDrawingRunsToEndOfFile() throws {
        let note = try Note(parsing: "```wp id=d1\nbox a\n+++page\n")
        XCTAssertEqual(note.pages, [NotePage(blocks: [.drawing(Drawing(id: "d1", source: "box a\n+++page\n"))])])
    }

    func testMissingAndDuplicateDrawingIDsAreAssigned() throws {
        let note = try Note(parsing: """
        ```wp
        box a
        ```
        ```wp id=d7
        box b
        ```
        ```wp id=d7
        box c
        ```
        ```wp id=has space
        box d
        ```
        """)
        XCTAssertEqual(note.drawings.map(\.id), ["d8", "d7", "d9", "has"])
        XCTAssertEqual(note.nextDrawingID(), "d10")
    }

    func testDrawingIDsAreNotReusedAfterDeletion() throws {
        var note = try Note(parsing: "```wp id=d1\n```\n```wp id=d2\n```")
        try note.deleteDrawing("d2")
        XCTAssertEqual(note.nextDrawingID(), "d3")
        let reloaded = try Note(parsing: note.serialized())
        XCTAssertEqual(reloaded.nextDrawingID(), "d3")
    }

    func testNextDrawingIDStartsAtOne() {
        XCTAssertEqual(Note().nextDrawingID(), "d1")
    }

    func testPagesCanNeverBeEmpty() {
        var note = Note(pages: [])
        XCTAssertEqual(note.pages, [NotePage()])
        note.pages = []
        XCTAssertEqual(note.pages, [NotePage()])
    }

    func testVersionFieldCannotBeChangedOrRemoved() {
        var frontMatter = FrontMatter()
        frontMatter["whiteprint"] = "9"
        frontMatter["whiteprint"] = nil
        XCTAssertEqual(frontMatter.formatVersion, Note.formatVersion)
    }

    func testFrontMatterValuesStaySingleLine() {
        var frontMatter = FrontMatter()
        frontMatter.title = "Two\nlines"
        XCTAssertEqual(frontMatter.title, "Two lines")
        frontMatter.title = nil
        XCTAssertNil(frontMatter.title)
    }

    func testIconAndCoverAreFrontMatterFields() throws {
        let note = try Note(parsing: "---\nwhiteprint: 1\ntitle: Trip\nicon: 🧭\ncover: monet-water-lilies\n---\n\nHi\n")
        XCTAssertEqual(note.frontMatter.icon, "🧭")
        XCTAssertEqual(note.frontMatter.cover, "monet-water-lilies")
        XCTAssertEqual(try Note(parsing: note.serialized()), note)
    }

    func testSettingIconAndCoverWritesAndRemovesFields() {
        var frontMatter = FrontMatter()
        frontMatter.title = "Trip"
        frontMatter.icon = "🌊"
        frontMatter.cover = "turner-fighting-temeraire"
        XCTAssertEqual(frontMatter.fields.map(\.key), ["whiteprint", "title", "icon", "cover"])
        XCTAssertEqual(frontMatter["icon"], "🌊")
        frontMatter.icon = "👩‍🚀"
        XCTAssertEqual(frontMatter.icon, "👩‍🚀")
        frontMatter.icon = nil
        frontMatter.cover = "  "
        XCTAssertNil(frontMatter.icon)
        XCTAssertNil(frontMatter.cover)
        XCTAssertEqual(frontMatter.fields.map(\.key), ["whiteprint", "title"])
    }

    func testBlankIconOrCoverReadsAsNone() throws {
        let note = try Note(parsing: "---\nwhiteprint: 1\nicon:\ncover:   \n---\n")
        XCTAssertNil(note.frontMatter.icon)
        XCTAssertNil(note.frontMatter.cover)
    }

    // MARK: Serializing

    func testSerializesCanonicalForm() {
        let note = Note(title: "Sketch", pages: [
            NotePage(blocks: [.text("# One"), .drawing(Drawing(id: "d1", source: "box a"))]),
            NotePage(blocks: [.text("Two")]),
        ])
        XCTAssertEqual(note.serialized(), """
        ---
        whiteprint: 1
        title: Sketch
        last-drawing: 1
        ---

        # One

        ```wp id=d1
        box a
        ```

        +++page

        Two

        """)
    }

    func testRoundTrips() throws {
        let notes = [
            Note(),
            Note(title: "T", pages: [NotePage(), NotePage(), NotePage(blocks: [.text("x")])]),
            Note(pages: [NotePage(blocks: [
                .drawing(Drawing(id: "d1", source: "")),
                .drawing(Drawing(id: "d2", source: "\n")),
                .drawing(Drawing(id: "d3", source: "box a\n```\nbox b")),
                .text("Para one\n\nPara two\n```swift\nlet x = 1\n```"),
                .drawing(Drawing(id: "d4", source: "flow a>b")),
            ])]),
        ]
        for note in notes {
            XCTAssertEqual(try Note(parsing: note.serialized()), note)
        }
    }

    func testSerializingIsIdempotentForMessyInput() throws {
        let messy = "\n\n# Hi   \n\n\n```wp\nbox a\n```\n+++page  \n\n\ntext\n"
        let once = try Note(parsing: messy).serialized()
        XCTAssertEqual(try Note(parsing: once).serialized(), once)
    }

    func testDrawingContainingFencesGetsLongerFence() throws {
        let drawing = Drawing(id: "d1", source: "text \"x\"\n````\n```")
        let text = Note(pages: [NotePage(blocks: [.drawing(drawing)])]).serialized()
        XCTAssertTrue(text.contains("`````wp id=d1\n"))
        XCTAssertEqual(try Note(parsing: text).drawings, [drawing])
    }

    func testTextWithUnclosedFenceIsClosedSoLaterBlocksSurvive() throws {
        let note = Note(pages: [NotePage(blocks: [
            .text("```\ncode"),
            .drawing(Drawing(id: "d1", source: "box a")),
        ])])
        let parsed = try Note(parsing: note.serialized())
        XCTAssertEqual(parsed.pages[0].blocks, [.text("```\ncode\n```"), .drawing(Drawing(id: "d1", source: "box a"))])
    }
}
