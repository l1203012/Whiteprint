import Foundation
import XCTest
import WhiteprintCore
@testable import WhiteprintBridge

/// Replies `ok` with a summary of the request; never replies to `listImports`.
private final class TestHandler: BridgeHandler {
    private let lock = NSLock()
    private var offMain = 0

    var calledOffMain: Bool {
        lock.lock()
        defer { lock.unlock() }
        return offMain > 0
    }

    func handle(_ request: BridgeRequest, reply: @escaping (BridgeResponse) -> Void) {
        if !Thread.isMainThread {
            lock.lock()
            offMain += 1
            lock.unlock()
        }
        switch request {
        case .ping:
            reply(.ok("pong"))
        case let .createNote(title, markdown, _):
            DispatchQueue.global().async { reply(.ok("\(title) \(markdown?.count ?? 0)")) }
        case .listImports:
            break
        case let .addPage(note):
            reply(.failure("no note '\(note)'"))
            reply(.ok("second reply is ignored"))
        default:
            reply(.ok("\(request)"))
        }
    }
}

private final class Results {
    private let lock = NSLock()
    private var replies = [BridgeResponse?](repeating: nil, count: 16)

    var all: [BridgeResponse?] {
        lock.lock()
        defer { lock.unlock() }
        return replies
    }

    func set(_ i: Int, _ reply: BridgeResponse?) {
        lock.lock()
        replies[i] = reply
        lock.unlock()
    }
}

private var socketCounter = 0

final class BridgeSocketTests: XCTestCase {
    private var path = ""
    private var handler: TestHandler!
    private var server: BridgeServer!

    override func setUp() {
        socketCounter += 1
        path = "/tmp/wpb-\(getpid())-\(socketCounter).sock"
        unlink(path)
        handler = TestHandler()
        server = BridgeServer(socketURL: URL(fileURLWithPath: path), handler: handler)
    }

    override func tearDown() {
        server.stop()
        unlink(path)
    }

    private var client: BridgeClient {
        BridgeClient(socketURL: URL(fileURLWithPath: path))
    }

    /// Runs blocking work on a background thread so the main queue stays free for the handler.
    private func background<T>(_ body: @escaping () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global().async {
                continuation.resume(with: Result { try body() })
            }
        }
    }

    /// Writes raw bytes on one connection and reads `count` reply lines.
    private func exchange(_ bytes: String, replies count: Int) throws -> [String] {
        let fd = try UnixSocket.make()
        defer { close(fd) }
        guard UnixSocket.connect(fd, try UnixSocket.address(path)) == 0 else { throw BridgeError.appNotRunning }
        var timeout = timeval(tv_sec: 5, tv_usec: 0)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        XCTAssertTrue(UnixSocket.writeAll(fd, Data(bytes.utf8)))
        var received = Data()
        var chunk = [UInt8](repeating: 0, count: 4096)
        while received.filter({ $0 == 0x0A }).count < count {
            let n = read(fd, &chunk, chunk.count)
            guard n > 0 else { break }
            received.append(contentsOf: chunk[0..<n])
        }
        return String(decoding: received, as: UTF8.self).split(separator: "\n").map(String.init)
    }

    private func encoded(_ request: BridgeRequest) throws -> String {
        String(decoding: try JSONEncoder().encode(request), as: UTF8.self)
    }

    private func decoded(_ line: String) throws -> BridgeResponse {
        try JSONDecoder().decode(BridgeResponse.self, from: Data(line.utf8))
    }

    func testRoundTripCallsHandlerOnMainQueue() async throws {
        try server.start()
        let client = self.client
        let replies = try await background { () -> [BridgeResponse] in
            [try client.send(.ping), try client.send(.createNote(title: "T", markdown: "abc", folder: nil)),
             try client.send(.readNote(note: "n1", page: 2))]
        }
        XCTAssertEqual(replies, [.ok("pong"), .ok("T 3"), .ok("\(BridgeRequest.readNote(note: "n1", page: 2))")])
        XCTAssertFalse(handler.calledOffMain)
    }

    func testWireFormatIsOneJSONObjectPerLine() throws {
        let line = try encoded(.write(note: "n1", page: 1, markdown: "a\nb", mode: .replace))
        XCTAssertFalse(line.contains("\n"))
        XCTAssertEqual(try JSONDecoder().decode(BridgeRequest.self, from: Data(line.utf8)),
                       .write(note: "n1", page: 1, markdown: "a\nb", mode: .replace))
        let plan = StudyPlan(title: "T", overview: "O", modules: [
            StudyModule(title: "M", minutes: 5, points: [StudyPoint(text: "p", importance: .good, ref: "r", topic: "t")]),
        ], tasks: [StudyTask(text: "do", due: "mon")], diagrams: ["box a"])
        let request = BridgeRequest.buildStudyPlan(importIDs: ["i1"], plan: plan)
        XCTAssertEqual(try JSONDecoder().decode(BridgeRequest.self, from: Data(try encoded(request).utf8)), request)
    }

    func testLargePayload() async throws {
        try server.start()
        let markdown = String(repeating: "Lorem ipsum dolor sit amet.\n", count: 60_000)
        XCTAssertGreaterThan(markdown.utf8.count, 1_500_000)
        let client = self.client
        let reply = try await background { try client.send(.createNote(title: "Big", markdown: markdown, folder: nil)) }
        XCTAssertEqual(reply, .ok("Big \(markdown.count)"))
    }

    func testConcurrentClients() async throws {
        try server.start()
        let path = self.path
        let replies = try await background { () -> [BridgeResponse?] in
            let results = Results()
            DispatchQueue.concurrentPerform(iterations: 16) { i in
                let reply = try? BridgeClient(socketURL: URL(fileURLWithPath: path))
                    .send(.createNote(title: "c\(i)", markdown: String(repeating: "x", count: i * 10_000), folder: nil), timeout: 10)
                results.set(i, reply)
            }
            return results.all
        }
        XCTAssertEqual(replies, (0..<16).map { .ok("c\($0) \($0 * 10_000)") })
    }

    func testPipelinedRequestsAndMalformedLines() async throws {
        try server.start()
        let input = "garbage\n\(try encoded(.ping))\n\n{\"unknownCase\":{}}\n\(try encoded(.createNote(title: "A", markdown: nil, folder: nil)))\n"
        let lines = try await background { try self.exchange(input, replies: 4) }
        XCTAssertEqual(try lines.map(decoded), [.failure("malformed request"), .ok("pong"), .failure("malformed request"), .ok("A 0")])
    }

    func testSecondReplyIsIgnored() async throws {
        try server.start()
        let input = "\(try encoded(.addPage(note: "n9")))\n\(try encoded(.ping))\n"
        let lines = try await background { try self.exchange(input, replies: 2) }
        XCTAssertEqual(try lines.map(decoded), [.failure("no note 'n9'"), .ok("pong")])
    }

    func testClientTimesOut() async throws {
        try server.start()
        let client = self.client
        let started = Date()
        let error = try await background { () -> Error? in
            do {
                _ = try client.send(.listImports, timeout: 0.5)
                return nil
            } catch {
                return error
            }
        }
        XCTAssertEqual(error as? BridgeError, .timedOut(0.5))
        XCTAssertLessThan(Date().timeIntervalSince(started), 3)
    }

    func testClientReportsAppNotRunning() {
        XCTAssertThrowsError(try client.send(.ping)) { error in
            XCTAssertEqual(error as? BridgeError, .appNotRunning)
        }
        FileManager.default.createFile(atPath: path, contents: nil)
        XCTAssertThrowsError(try client.send(.ping)) { error in
            XCTAssertEqual(error as? BridgeError, .appNotRunning)
        }
    }

    func testSocketFileIsPrivateAndRemovedOnStop() throws {
        try server.start()
        let attributes = try FileManager.default.attributesOfItem(atPath: path)
        XCTAssertEqual((attributes[.posixPermissions] as? NSNumber)?.intValue, 0o600)
        server.stop()
        XCTAssertFalse(FileManager.default.fileExists(atPath: path))
        XCTAssertThrowsError(try client.send(.ping)) { error in
            XCTAssertEqual(error as? BridgeError, .appNotRunning)
        }
    }

    func testStaleSocketFileIsReplaced() async throws {
        let fd = try UnixSocket.make()
        XCTAssertEqual(UnixSocket.bind(fd, try UnixSocket.address(path)), 0)
        close(fd)
        XCTAssertTrue(FileManager.default.fileExists(atPath: path))
        try server.start()
        let client = self.client
        let reply = try await background { try client.send(.ping) }
        XCTAssertEqual(reply, .ok("pong"))
    }

    func testRefusesToStealALiveSocket() throws {
        try server.start()
        let second = BridgeServer(socketURL: URL(fileURLWithPath: path), handler: handler)
        XCTAssertThrowsError(try second.start()) { error in
            XCTAssertEqual(error as? BridgeError, .alreadyRunning(path))
        }
        XCTAssertTrue(UnixSocket.isAccepting(path))
    }

    func testStartIsIdempotent() throws {
        try server.start()
        try server.start()
        XCTAssertTrue(UnixSocket.isAccepting(path))
    }

    func testLongPathsAreRejected() {
        let long = "/tmp/" + String(repeating: "x", count: 120) + ".sock"
        XCTAssertThrowsError(try BridgeServer(socketURL: URL(fileURLWithPath: long), handler: handler).start()) { error in
            XCTAssertEqual(error as? BridgeError, .pathTooLong(long))
        }
        XCTAssertThrowsError(try BridgeClient(socketURL: URL(fileURLWithPath: long)).send(.ping)) { error in
            XCTAssertEqual(error as? BridgeError, .pathTooLong(long))
        }
    }

    func testErrorDescriptions() {
        XCTAssertEqual(BridgeError.appNotRunning.description, "Whiteprint isn't running")
        XCTAssertEqual(BridgeError.timedOut(120).description, "Whiteprint didn't reply within 120 s")
        XCTAssertEqual(BridgeError.system("bind", errno: EACCES).description, "bind failed: Permission denied")
    }

    func testSocketPathOverride() {
        XCTAssertEqual(BridgePaths.socketEnvironmentKey, "WHITEPRINT_SOCKET")
        setenv("WHITEPRINT_SOCKET", "/tmp/wpb-override.sock", 1)
        XCTAssertEqual(BridgePaths.socket.path, "/tmp/wpb-override.sock")
        unsetenv("WHITEPRINT_SOCKET")
        XCTAssertEqual(BridgePaths.socket.lastPathComponent, "mcp.sock")
    }

    func testBundleIsFoundFromHelperPath() {
        XCTAssertEqual(AppLauncher.bundle(containing: URL(fileURLWithPath: "/Applications/Whiteprint.app/Contents/MacOS/whiteprint-mcp"))?.path,
                       "/Applications/Whiteprint.app")
        XCTAssertNil(AppLauncher.bundle(containing: URL(fileURLWithPath: "/usr/local/bin/whiteprint-mcp")))
        XCTAssertNil(AppLauncher.bundle(containing: URL(fileURLWithPath: "/tmp/Contents/MacOS/whiteprint-mcp")))
        XCTAssertNil(AppLauncher.containingBundle())
    }

    func testEnsureRunningReturnsWhenServerIsUp() throws {
        try server.start()
        XCTAssertNoThrow(try AppLauncher.ensureRunning(socketURL: URL(fileURLWithPath: path), timeout: 1))
    }
}
