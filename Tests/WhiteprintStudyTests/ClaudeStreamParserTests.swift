import Foundation
import XCTest
@testable import WhiteprintStudy

/// Hand-written in Claude Code's `--output-format stream-json --verbose` shape.
enum StreamSamples {
    static let initLine = #"{"type":"system","subtype":"init","cwd":"/private/var/folders/x/T/Whiteprint-1","session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11","tools":["mcp__whiteprint__list_imports","mcp__whiteprint__read_chunk","mcp__whiteprint__save_points","mcp__whiteprint__get_points","mcp__whiteprint__build_study_plan","ReadMcpResourceTool"],"mcp_servers":[{"name":"whiteprint","status":"connected"}],"model":"claude-sonnet-4-5","permissionMode":"dontAsk","apiKeySource":"none"}"#

    static let run = [
        initLine,
        #"{"type":"assistant","message":{"id":"msg_01","type":"message","role":"assistant","model":"claude-sonnet-4-5","content":[{"type":"text","text":"I'll start by listing your imports."},{"type":"tool_use","id":"toolu_01","name":"mcp__whiteprint__list_imports","input":{}}],"stop_reason":"tool_use","usage":{"input_tokens":1200,"output_tokens":40}},"parent_tool_use_id":null,"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"user","message":{"role":"user","content":[{"tool_use_id":"toolu_01","type":"tool_result","content":[{"type":"text","text":"i1 Lecture3.pptx 24 12 0/12"}]}]},"parent_tool_use_id":null,"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"assistant","message":{"id":"msg_02","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_02","name":"mcp__whiteprint__read_chunk","input":{"import":"i1","n":3}}]},"parent_tool_use_id":null,"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"user","message":{"role":"user","content":[{"tool_use_id":"toolu_02","type":"tool_result","content":[{"type":"text","text":"Lecture3.pptx · chunk 3/12 · slide 5–slide 7\n\n[slide 5]\nTCP"}]}]},"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"assistant","message":{"id":"msg_03","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_03","name":"mcp__whiteprint__save_points","input":{"import":"i1","n":3,"points":[{"text":"TCP is connection-oriented","importance":"must","ref":"slide 5"}]}}]},"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"user","message":{"role":"user","content":[{"tool_use_id":"toolu_03","type":"tool_result","content":[{"type":"text","text":"ok"}]}]},"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"assistant","message":{"id":"msg_04","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_04","name":"ReadMcpResourceTool","input":{"server":"whiteprint","uri":"whiteprint://dsl"}}]},"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"assistant","message":{"id":"msg_05","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_05","name":"mcp__whiteprint__get_points","input":{}}]},"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"assistant","message":{"id":"msg_06","type":"message","role":"assistant","content":[{"type":"tool_use","id":"toolu_06","name":"mcp__whiteprint__build_study_plan","input":{"imports":["i1"],"plan":{"title":"Networks"}}}]},"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"assistant","message":{"id":"msg_07","type":"message","role":"assistant","content":[{"type":"text","text":"Study plan ready: 4 modules, 3 tasks."}]},"session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11"}"#,
        #"{"type":"result","subtype":"success","is_error":false,"duration_ms":81234,"duration_api_ms":79012,"num_turns":9,"result":"Study plan ready: 4 modules, 3 tasks.","session_id":"5f1c6c1e-0d5a-4c1e-9d43-2a3f7e0b9c11","total_cost_usd":0.21,"usage":{"input_tokens":30211,"output_tokens":2210},"permission_denials":[]}"#,
    ]

    static let notLoggedIn = #"{"type":"result","subtype":"success","is_error":true,"duration_ms":312,"num_turns":1,"result":"Invalid API key · Please run /login","session_id":"0b6a","total_cost_usd":0}"#
    static let maxTurns = #"{"type":"result","subtype":"error_max_turns","is_error":true,"duration_ms":1000,"num_turns":50,"session_id":"0b6a"}"#
    static let helperFailed = #"{"type":"system","subtype":"init","session_id":"0b6a","tools":[],"mcp_servers":[{"name":"whiteprint","status":"failed"}]}"#
}

final class ClaudeStreamParserTests: XCTestCase {
    private typealias Event = ClaudeCodeRunner.Event

    private let lecture = StudyImport(id: "i1", name: "Lecture3.pptx", unitCount: 24, chunkCount: 12, chunksDone: 0, importedAt: Date())

    private func parse(_ lines: [String], imports: [StudyImport] = []) -> [Event] {
        var parser = ClaudeStreamParser { id in imports.first { $0.id == id } }
        return parser.feed(Data((lines.joined(separator: "\n") + "\n").utf8)) + parser.finish()
    }

    func testRecordedRun() {
        XCTAssertEqual(parse(StreamSamples.run, imports: [lecture]), [
            .status("Starting Claude…"),
            .text("I'll start by listing your imports."),
            .status("Looking at your imports…"),
            .status("Reading chunk 3 of 12 · Lecture3.pptx"),
            .status("Saving points…"),
            .status("Reading the drawing reference…"),
            .status("Merging the points…"),
            .status("Building the study plan…"),
            .text("Study plan ready: 4 modules, 3 tasks."),
            .finished,
        ])
    }

    func testUnknownImportStillReportsProgress() {
        XCTAssertEqual(parse([StreamSamples.run[3]]), [.status("Reading chunk 3 · i1")])
    }

    func testBytesSplitAnywhere() {
        let bytes = Array((StreamSamples.run.joined(separator: "\n") + "\n").utf8)
        var parser = ClaudeStreamParser { [lecture] id in id == "i1" ? lecture : nil }
        var events: [Event] = []
        var offset = 0
        for size in [1, 7, 300, 2, 4096, 13] where offset < bytes.count {
            let end = min(offset + size, bytes.count)
            events += parser.feed(Data(bytes[offset..<end]))
            offset = end
        }
        events += parser.feed(Data(bytes[offset...]))
        XCTAssertEqual(events, parse(StreamSamples.run, imports: [lecture]))
        XCTAssertTrue(parser.isFinished)
    }

    func testTrailingLineWithoutNewline() {
        var parser = ClaudeStreamParser()
        XCTAssertEqual(parser.feed(Data(StreamSamples.run.last!.utf8)), [])
        XCTAssertEqual(parser.finish(), [.finished])
    }

    func testErrorResults() {
        XCTAssertEqual(parse([StreamSamples.notLoggedIn]), [.failed("Invalid API key · Please run /login")])
        XCTAssertEqual(parse([StreamSamples.maxTurns]), [.failed("Claude stopped after too many steps. Run it again to continue.")])
        XCTAssertEqual(parse([#"{"type":"result","subtype":"error_during_execution","is_error":true}"#]), [.failed("Claude Code ran into an error (error_during_execution).")])
    }

    func testHelperThatFailedToStartEndsTheRun() {
        XCTAssertEqual(parse([StreamSamples.helperFailed] + StreamSamples.run), [
            .failed("Claude Code couldn't start Whiteprint's tools. Try reinstalling Whiteprint."),
        ])
    }

    func testNoiseIsIgnored() {
        XCTAssertEqual(parse([
            "",
            "Warning: something on stdout",
            #"{"type":"stream_event","event":{}}"#,
            #"[1,2]"#,
            #"{"type":"assistant","message":{"content":[{"type":"thinking","thinking":"hmm"},{"type":"text","text":"  "}]}}"#,
        ]), [])
    }

    func testNothingAfterTheResult() {
        XCTAssertEqual(parse([StreamSamples.run.last!, StreamSamples.run[1]]), [.finished])
    }
}
