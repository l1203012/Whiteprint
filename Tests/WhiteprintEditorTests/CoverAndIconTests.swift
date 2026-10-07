import AppKit
import XCTest
import WhiteprintCore
@testable import WhiteprintEditor

final class CoverAndIconTests: XCTestCase {
    private var window: NSWindow!
    private var editor: NoteEditorView!

    override func setUp() {
        _ = NSApplication.shared
    }

    override func tearDown() {
        window?.close()
        window = nil
        editor = nil
    }

    private func open(_ text: String) throws {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 800), styleMask: [.titled, .resizable],
                          backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        editor = NoteEditorView(note: try Note(parsing: text))
        window.contentView = editor
        editor.layoutSubtreeIfNeeded()
    }

    private var firstBlockTop: CGFloat {
        editor.pageViews[0].blockViews[0].frame.minY
    }

    func testEveryGalleryCoverHasAnImage() {
        for cover in CoverGallery.covers {
            XCTAssertNotNil(CoverGallery.image(id: cover.id), cover.id)
        }
        XCTAssertNil(CoverGallery.image(id: "no-such-cover"))
        XCTAssertEqual(Set(CoverGallery.covers.map(\.id)).count, CoverGallery.covers.count)
    }

    func testGalleryFolderHoldsOnlyListedCovers() throws {
        let folder = try XCTUnwrap(CoverGallery.folder)
        let files = try FileManager.default.contentsOfDirectory(atPath: folder.path).filter { $0.hasSuffix(".jpg") }
        XCTAssertEqual(Set(files), Set(CoverGallery.covers.map { $0.id + ".jpg" }))
    }

    func testSettingACoverIsUndoableAndReported() throws {
        try open("# Trip")
        var reported: Note?
        editor.onChange = { reported = $0 }
        let plainTop = firstBlockTop

        editor.setCover("turner-fighting-temeraire")
        XCTAssertEqual(reported?.frontMatter.cover, "turner-fighting-temeraire")
        XCTAssertEqual(editor.note.frontMatter.cover, "turner-fighting-temeraire")
        editor.layoutSubtreeIfNeeded()
        XCTAssertGreaterThan(firstBlockTop, plainTop + PageHeaderView.minCoverHeight - 1)

        XCTAssertEqual(window.undoManager?.undoActionName, "Change Cover")
        window.undoManager?.undo()
        XCTAssertNil(editor.note.frontMatter.cover)
        editor.layoutSubtreeIfNeeded()
        XCTAssertEqual(firstBlockTop, plainTop)
    }

    func testIconSitsAboveTheFirstBlock() throws {
        try open("---\nwhiteprint: 1\nicon: 🧭\n---\n\n# Trip")
        let header = try XCTUnwrap(editor.pageViews[0].header)
        XCTAssertEqual(header.icon, "🧭")
        XCTAssertLessThan(header.iconRect.maxY + header.frame.minY, firstBlockTop)
        editor.setIcon(nil)
        XCTAssertNil(editor.note.frontMatter.icon)
        XCTAssertEqual(editor.note.pages, try Note(parsing: "# Trip").pages)
    }

    func testHidingCoversKeepsThemInTheNote() throws {
        try open("---\nwhiteprint: 1\ncover: van-gogh-starry-night\n---\n\nText")
        let withCover = firstBlockTop
        editor.showsCoverAndIcon = false
        editor.layoutSubtreeIfNeeded()
        XCTAssertNil(editor.pageViews[0].header)
        XCTAssertLessThan(firstBlockTop, withCover)
        XCTAssertEqual(editor.note.frontMatter.cover, "van-gogh-starry-night")
    }

    func testAnExternalEditShowsTheNewIcon() throws {
        try open("Text")
        var note = editor.note
        note.frontMatter.icon = "🌊"
        editor.setNote(note, preservingSelection: true)
        XCTAssertEqual(editor.pageViews[0].header?.icon, "🌊")
    }

    func testFillSourceCropsAroundTheCentre() {
        let source = PageHeaderView.fillSource(imageSize: NSSize(width: 1000, height: 400), target: NSSize(width: 500, height: 100))
        XCTAssertEqual(source, NSRect(x: 0, y: 100, width: 1000, height: 200))
    }

    func testFirstEmojiSkipsPlainText() {
        XCTAssertEqual(IconPicker.firstEmoji(in: "ab🧭c"), "🧭")
        XCTAssertEqual(IconPicker.firstEmoji(in: "❤️"), "❤️")
        XCTAssertNil(IconPicker.firstEmoji(in: "abc 1"))
    }
}
