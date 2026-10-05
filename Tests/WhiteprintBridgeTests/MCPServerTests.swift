import Foundation
import XCTest
import WhiteprintCore
@testable import WhiteprintBridge

final class MCPServerTests: XCTestCase {
    private var sent: [BridgeRequest] = []
    private var server: MCPServer!

    override func setUp() {
        sent = []
        server = MCPServer { [unowned self] request in
            self.sent.append(request)
            return .ok("ok")
        }
    }

    private func call(_ method: String, _ params: [String: Any]? = nil, id: Any = 1) throws -> [String: Any] {
        var message: [String: Any] = ["jsonrpc": "2.0", "id": id, "method": method]
        message["params"] = params
        let line = String(decoding: try JSONSerialization.data(withJSONObject: message), as: UTF8.self)
        return try parse(XCTUnwrap(server.handle(line: line)))
    }

    private func parse(_ line: String) throws -> [String: Any] {
        XCTAssertFalse(line.contains("\n"))
        return try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any])
    }

    private func result(_ method: String, _ params: [String: Any]? = nil) throws -> [String: Any] {
        let response = try call(method, params)
        XCTAssertNil(response["error"], "\(method): \(response)")
        return try XCTUnwrap(response["result"] as? [String: Any])
    }

    private func errorCode(_ response: [String: Any]) -> Int? {
        (response["error"] as? [String: Any])?["code"] as? Int
    }

    func testInitializeEchoesSupportedVersion() throws {
        for version in ["2025-06-18", "2025-03-26", "2024-11-05"] {
            let r = try result("initialize", ["protocolVersion": version, "capabilities": [:], "clientInfo": ["name": "t", "version": "1"]])
            XCTAssertEqual(r["protocolVersion"] as? String, version)
        }
    }

    func testInitializeOffersNewestForUnknownVersion() throws {
        XCTAssertEqual(try result("initialize", ["protocolVersion": "2099-01-01"])["protocolVersion"] as? String, "2025-06-18")
        XCTAssertEqual(try result("initialize")["protocolVersion"] as? String, "2025-06-18")
    }

    func testInitializeDescribesServer() throws {
        let r = try result("initialize", ["protocolVersion": "2025-06-18"])
        let capabilities = try XCTUnwrap(r["capabilities"] as? [String: Any])
        XCTAssertEqual(Set(capabilities.keys), ["tools", "resources", "prompts"])
        let info = try XCTUnwrap(r["serverInfo"] as? [String: Any])
        XCTAssertEqual(info["name"] as? String, "whiteprint")
        XCTAssertNotNil(info["version"] as? String)
        XCTAssertTrue((r["instructions"] as? String ?? "").contains("whiteprint://dsl"))
    }

    func testResponsesEchoIDs() throws {
        XCTAssertEqual(try call("ping", id: 7)["id"] as? Int, 7)
        XCTAssertEqual(try call("ping", id: "abc")["id"] as? String, "abc")
        XCTAssertEqual(try call("ping")["jsonrpc"] as? String, "2.0")
    }

    func testNotificationsGetNoReply() {
        XCTAssertNil(server.handle(line: #"{"jsonrpc":"2.0","method":"notifications/initialized"}"#))
        XCTAssertNil(server.handle(line: #"{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1}}"#))
        XCTAssertNil(server.handle(line: #"{"jsonrpc":"2.0","method":"tools/call","params":{"name":"list_notes"}}"#))
        XCTAssertTrue(sent.isEmpty)
    }

    func testClientResponsesGetNoReply() {
        XCTAssertNil(server.handle(line: #"{"jsonrpc":"2.0","id":3,"result":{}}"#))
    }

    func testPing() throws {
        XCTAssertTrue(try result("ping").isEmpty)
    }

    func testParseError() throws {
        let r = try parse(XCTUnwrap(server.handle(line: "{not json")))
        XCTAssertEqual(errorCode(r), -32700)
        XCTAssertTrue(r["id"] is NSNull)
    }

    func testInvalidRequests() throws {
        for line in [#"{"id":1,"method":"ping"}"#, #"{"jsonrpc":"2.0","id":1}"#, #"{"jsonrpc":"2.0","id":1,"method":5}"#, "42", "[]"] {
            XCTAssertEqual(errorCode(try parse(XCTUnwrap(server.handle(line: line)))), -32600, line)
        }
    }

    func testUnknownMethod() throws {
        XCTAssertEqual(errorCode(try call("nope/nope")), -32601)
    }

    func testBatch() throws {
        let line = #"[{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/initialized"},{"jsonrpc":"2.0","id":2,"method":"x"}]"#
        let replies = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(XCTUnwrap(server.handle(line: line)).utf8)) as? [[String: Any]])
        XCTAssertEqual(replies.count, 2)
        XCTAssertEqual(replies.compactMap { $0["id"] as? Int }, [1, 2])
    }

    func testResources() throws {
        let list = try XCTUnwrap(try result("resources/list")["resources"] as? [[String: Any]])
        XCTAssertEqual(list.count, 1)
        XCTAssertEqual(list[0]["uri"] as? String, "whiteprint://dsl")
        XCTAssertEqual(list[0]["mimeType"] as? String, "text/plain")

        let contents = try XCTUnwrap(try result("resources/read", ["uri": "whiteprint://dsl"])["contents"] as? [[String: Any]])
        XCTAssertEqual(contents[0]["text"] as? String, WhiteprintText.dslReference)
        XCTAssertEqual(contents[0]["mimeType"] as? String, "text/plain")
        XCTAssertEqual(contents[0]["uri"] as? String, "whiteprint://dsl")

        XCTAssertEqual(errorCode(try call("resources/read", ["uri": "whiteprint://nope"])), -32002)
        XCTAssertEqual(errorCode(try call("resources/read", [:])), -32602)
        XCTAssertNotNil(try result("resources/templates/list")["resourceTemplates"] as? [Any])
    }

    func testPrompts() throws {
        let list = try XCTUnwrap(try result("prompts/list")["prompts"] as? [[String: Any]])
        XCTAssertEqual(list.map { $0["name"] as? String }, ["study_plan"])
        let arguments = try XCTUnwrap(list[0]["arguments"] as? [[String: Any]])
        XCTAssertEqual(arguments[0]["name"] as? String, "imports")
        XCTAssertEqual(arguments[0]["required"] as? Bool, false)

        XCTAssertEqual(try promptText([:]), WhiteprintText.studyPlanPrompt)
        XCTAssertEqual(try promptText(["imports": "i1, i2"]), WhiteprintText.studyPlanPrompt + "\n\nImports: i1, i2")
        XCTAssertEqual(errorCode(try call("prompts/get", ["name": "other"])), -32602)
        XCTAssertEqual(errorCode(try call("prompts/get", [:])), -32602)
    }

    private func promptText(_ arguments: [String: Any]) throws -> String? {
        let r = try result("prompts/get", ["name": "study_plan", "arguments": arguments])
        let messages = try XCTUnwrap(r["messages"] as? [[String: Any]])
        XCTAssertEqual(messages.count, 1)
        XCTAssertEqual(messages[0]["role"] as? String, "user")
        let content = try XCTUnwrap(messages[0]["content"] as? [String: Any])
        XCTAssertEqual(content["type"] as? String, "text")
        return content["text"] as? String
    }

    func testToolCallReturnsReplyText() throws {
        let r = try result("tools/call", ["name": "add_page", "arguments": ["note": "n1"]])
        XCTAssertEqual(r["isError"] as? Bool, false)
        let content = try XCTUnwrap(r["content"] as? [[String: Any]])
        XCTAssertEqual(content.count, 1)
        XCTAssertEqual(content[0]["type"] as? String, "text")
        XCTAssertEqual(content[0]["text"] as? String, "ok")
        XCTAssertEqual(sent, [.addPage(note: "n1")])
    }

    func testToolCallWithoutArguments() throws {
        _ = try result("tools/call", ["name": "list_notes"])
        XCTAssertEqual(sent, [.listNotes])
    }

    func testAppFailureIsErrorResult() throws {
        server = MCPServer { _ in .failure("no note 'n9'") }
        let r = try result("tools/call", ["name": "add_page", "arguments": ["note": "n9"]])
        XCTAssertEqual(r["isError"] as? Bool, true)
        XCTAssertEqual(((r["content"] as? [[String: Any]])?.first)?["text"] as? String, "no note 'n9'")
    }

    func testTransportErrorIsErrorResult() throws {
        server = MCPServer { _ in throw BridgeError.timedOut(120) }
        let r = try result("tools/call", ["name": "list_notes", "arguments": [:]])
        XCTAssertEqual(r["isError"] as? Bool, true)
        XCTAssertEqual(((r["content"] as? [[String: Any]])?.first)?["text"] as? String, "Whiteprint didn't reply within 120 s")
    }

    func testUnknownToolIsInvalidParams() throws {
        XCTAssertEqual(errorCode(try call("tools/call", ["name": "explode", "arguments": [:]])), -32602)
        XCTAssertEqual(errorCode(try call("tools/call", ["arguments": [:]])), -32602)
    }

    func testOutputEscapesNewlinesAndKeepsSlashes() throws {
        server = MCPServer { _ in .ok("line 1\nline 2 a/b") }
        let line = try XCTUnwrap(server.handle(line: #"{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_notes"}}"#))
        XCTAssertFalse(line.contains("\n"))
        XCTAssertTrue(line.contains("a/b"))
    }
}
