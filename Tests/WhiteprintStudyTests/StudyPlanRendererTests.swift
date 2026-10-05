import Foundation
import XCTest
import WhiteprintCore
@testable import WhiteprintStudy

final class StudyPlanRendererTests: XCTestCase {
    private let plan = StudyPlan(
        title: "Networks 101",
        overview: "Covers the TCP/IP stack.\nFocus on transport.",
        modules: [
            StudyModule(title: "Transport layer", minutes: 45, points: [
                StudyPoint(text: "Window size", importance: .good, ref: "L3.pptx · slide 7"),
                StudyPoint(text: "History of TCP", importance: .skip, ref: "L3.pptx · slide 1"),
                StudyPoint(text: "TCP is connection-oriented", importance: .must, ref: "L3.pptx · slide 2"),
                StudyPoint(text: "Mascot slide", importance: .skip, ref: "L3.pptx · slide 9"),
            ]),
            StudyModule(title: "Routing", minutes: 90, points: [
                StudyPoint(text: "Dijkstra", importance: .must, ref: "L4.pdf · p. 3"),
            ]),
        ],
        tasks: [
            StudyTask(text: "Read chapter 4", due: nil, ref: "Syllabus.pdf · p. 2"),
            StudyTask(text: "Lab 2", due: "week 6"),
            StudyTask(text: "Essay", due: "2026-03-01", ref: "Syllabus.pdf · p. 6"),
            StudyTask(text: "Quiz", due: "15.01.2026"),
        ],
        diagrams: ["flow App>TCP>IP", "  ", "box a \"Router\""]
    )

    func testPageOneHasOverviewAndLearningPath() throws {
        let note = StudyPlanRenderer.note(for: plan)
        XCTAssertEqual(note.frontMatter.title, "Networks 101")
        XCTAssertEqual(note.pages[0].blocks, [
            .text("""
            # Networks 101

            ## Overview · ≈ 2 h 15 min in total

            Covers the TCP/IP stack.
            Focus on transport.

            ★ must know · ○ good to know · ✕ can skip

            ## Learning path

            ### 1. Transport layer · ≈ 45 min

            - ★ **TCP is connection-oriented** — *L3.pptx · slide 2*
            - ○ Window size — *L3.pptx · slide 7*

            ✕ Can skip: History of TCP *(L3.pptx · slide 1)*; Mascot slide *(L3.pptx · slide 9)*

            ### 2. Routing · ≈ 1 h 30 min

            - ★ **Dijkstra** — *L4.pdf · p. 3*
            """),
        ])
    }

    func testToDoIsSortedByDueDate() {
        let note = StudyPlanRenderer.note(for: plan)
        XCTAssertEqual(note.pages[1].blocks, [.text("""
        ## To-do

        - [ ] Quiz — due 15.01.2026
        - [ ] Essay — due 2026-03-01 — *Syllabus.pdf · p. 6*
        - [ ] Lab 2 — due week 6
        - [ ] Read chapter 4 — *Syllabus.pdf · p. 2*
        """)])
    }

    func testDiagramsBecomeDrawings() {
        let note = StudyPlanRenderer.note(for: plan)
        XCTAssertEqual(note.pages.count, 3)
        XCTAssertEqual(note.pages[2].blocks.first, .text("## Diagrams"))
        XCTAssertEqual(note.drawings, [
            Drawing(id: "d1", source: "flow App>TCP>IP"),
            Drawing(id: "d2", source: "box a \"Router\""),
        ])
    }

    func testSerializedNoteParsesBackTheSame() throws {
        let note = StudyPlanRenderer.note(for: plan)
        let text = note.serialized()
        XCTAssertEqual(text.components(separatedBy: "\n+++page\n").count, 3)
        XCTAssertTrue(text.contains("```wp id=d1\nflow App>TCP>IP\n```"))
        XCTAssertEqual(try Note(parsing: text), note)
    }

    func testEmptyPagesAreLeftOut() {
        let note = StudyPlanRenderer.note(for: StudyPlan(title: " ", overview: "", modules: []))
        XCTAssertEqual(note.pages.count, 1)
        XCTAssertEqual(note.frontMatter.title, "Study plan")
        XCTAssertFalse(note.serialized().contains("in total"))
        XCTAssertFalse(note.serialized().contains("+++page"))
    }

    func testFreeTextCantBreakTheNoteStructure() throws {
        let hostile = StudyPlan(
            title: "T\n+++page",
            overview: "Intro\n+++page\n```wp\nbox x\n   ~~~\nend",
            modules: [StudyModule(title: "M\n```wp", points: [
                StudyPoint(text: "a\n+++page", importance: .must, ref: "r\n```"),
                StudyPoint(text: "b", importance: .skip, ref: ""),
            ])],
            tasks: [StudyTask(text: "x\n+++page", due: "soon\n```wp")]
        )
        let note = StudyPlanRenderer.note(for: hostile)
        let reparsed = try Note(parsing: note.serialized())
        XCTAssertEqual(reparsed, note)
        XCTAssertEqual(reparsed.pages.count, 2)
        XCTAssertTrue(reparsed.drawings.isEmpty)
        let separators = note.serialized().split(separator: "\n").filter { $0 == "+++page" }
        XCTAssertEqual(separators.count, note.pages.count - 1)
        XCTAssertTrue(note.serialized().contains("\\+++page"))
        XCTAssertTrue(note.serialized().contains("✕ Can skip: b\n"))
    }

    func testModuleWithoutMinutesHasNoEstimate() throws {
        let note = StudyPlanRenderer.note(for: StudyPlan(title: "T", overview: "o", modules: [
            StudyModule(title: "Only skips", points: [StudyPoint(text: "x", importance: .skip, ref: "p. 1")]),
        ]))
        guard case .text(let page) = note.pages[0].blocks[0] else { return XCTFail("no text") }
        XCTAssertTrue(page.hasSuffix("## Learning path\n\n### 1. Only skips\n\n✕ Can skip: x *(p. 1)*"))
    }
}
