// A minimal stand-in for XCTest, used by Scripts/test.sh when only the
// Command Line Tools are installed. Not part of the app.
import Foundation

open class XCTestCase {
    public required init() {}
    open func setUp() {}
    open func setUpWithError() throws {}
    open func tearDown() {}
}

private var failures: [String] = []

func XCTestShimRun(_ name: String, _ body: () async throws -> Void) async {
    let before = failures.count
    do {
        try await body()
    } catch is XCTSkip {
        print("- \(name) skipped")
    } catch {
        failures.append("\(name) threw \(error)")
    }
    if failures.count > before {
        print("✗ \(name)")
    }
}

func XCTestShimFinish(_ count: Int) -> Never {
    for failure in failures {
        print("FAIL", failure)
    }
    print("\(count) tests, \(failures.count) failures")
    exit(failures.isEmpty ? 0 : 1)
}

private func record(_ what: String, _ message: String, _ file: StaticString, _ line: UInt) {
    let name = ("\(file)" as NSString).lastPathComponent
    failures.append("\(name):\(line): \(what)\(message.isEmpty ? "" : " — \(message)")")
}

public struct XCTSkip: Error {
    public init(_ message: String = "") {}
}

func XCTAssertEqual<T: Equatable>(
    _ a: @autoclosure () throws -> T, _ b: @autoclosure () throws -> T, _ message: @autoclosure () -> String = "",
    file: StaticString = #filePath, line: UInt = #line
) {
    do {
        let x = try a(), y = try b()
        if x != y { record("XCTAssertEqual failed: (\(x)) != (\(y))", message(), file, line) }
    } catch {
        record("XCTAssertEqual threw \(error)", message(), file, line)
    }
}

func XCTAssertNotEqual<T: Equatable>(
    _ a: @autoclosure () throws -> T, _ b: @autoclosure () throws -> T, _ message: @autoclosure () -> String = "",
    file: StaticString = #filePath, line: UInt = #line
) {
    do {
        let x = try a(), y = try b()
        if x == y { record("XCTAssertNotEqual failed: (\(x)) == (\(y))", message(), file, line) }
    } catch {
        record("XCTAssertNotEqual threw \(error)", message(), file, line)
    }
}

func XCTAssertEqual<T: FloatingPoint>(
    _ a: @autoclosure () throws -> T, _ b: @autoclosure () throws -> T, accuracy: T,
    _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line
) {
    do {
        let x = try a(), y = try b()
        if abs(x - y) > accuracy { record("XCTAssertEqual failed: \(x) != \(y) ± \(accuracy)", message(), file, line) }
    } catch {
        record("XCTAssertEqual threw \(error)", message(), file, line)
    }
}

private func compare<T: Comparable>(
    _ name: String, _ a: () throws -> T, _ b: () throws -> T, _ ok: (T, T) -> Bool,
    _ message: String, _ file: StaticString, _ line: UInt
) {
    do {
        let x = try a(), y = try b()
        if !ok(x, y) { record("\(name) failed: \(x), \(y)", message, file, line) }
    } catch {
        record("\(name) threw \(error)", message, file, line)
    }
}

func XCTAssertGreaterThan<T: Comparable>(_ a: @autoclosure () throws -> T, _ b: @autoclosure () throws -> T, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    compare("XCTAssertGreaterThan", a, b, >, message(), file, line)
}

func XCTAssertGreaterThanOrEqual<T: Comparable>(_ a: @autoclosure () throws -> T, _ b: @autoclosure () throws -> T, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    compare("XCTAssertGreaterThanOrEqual", a, b, >=, message(), file, line)
}

func XCTAssertLessThan<T: Comparable>(_ a: @autoclosure () throws -> T, _ b: @autoclosure () throws -> T, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    compare("XCTAssertLessThan", a, b, <, message(), file, line)
}

func XCTAssertLessThanOrEqual<T: Comparable>(_ a: @autoclosure () throws -> T, _ b: @autoclosure () throws -> T, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    compare("XCTAssertLessThanOrEqual", a, b, <=, message(), file, line)
}

func XCTAssertTrue(_ e: @autoclosure () throws -> Bool, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    do {
        if try !e() { record("XCTAssertTrue failed", message(), file, line) }
    } catch {
        record("XCTAssertTrue threw \(error)", message(), file, line)
    }
}

func XCTAssert(_ e: @autoclosure () throws -> Bool, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    XCTAssertTrue(try e(), message(), file: file, line: line)
}

func XCTAssertFalse(_ e: @autoclosure () throws -> Bool, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    do {
        if try e() { record("XCTAssertFalse failed", message(), file, line) }
    } catch {
        record("XCTAssertFalse threw \(error)", message(), file, line)
    }
}

func XCTAssertNil<T>(_ e: @autoclosure () throws -> T?, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    do {
        if let value = try e() { record("XCTAssertNil failed: \(value)", message(), file, line) }
    } catch {
        record("XCTAssertNil threw \(error)", message(), file, line)
    }
}

func XCTAssertNotNil<T>(_ e: @autoclosure () throws -> T?, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    do {
        if try e() == nil { record("XCTAssertNotNil failed", message(), file, line) }
    } catch {
        record("XCTAssertNotNil threw \(error)", message(), file, line)
    }
}

struct XCTUnwrapError: Error {}

func XCTUnwrap<T>(_ e: @autoclosure () throws -> T?, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) throws -> T {
    if let value = try e() { return value }
    record("XCTUnwrap failed: nil", message(), file, line)
    throw XCTUnwrapError()
}

func XCTFail(_ message: String = "", file: StaticString = #filePath, line: UInt = #line) {
    record("XCTFail", message, file, line)
}

func XCTAssertThrowsError<T>(
    _ e: @autoclosure () throws -> T, _ message: @autoclosure () -> String = "",
    file: StaticString = #filePath, line: UInt = #line, _ handler: (Error) -> Void = { _ in }
) {
    do {
        _ = try e()
        record("XCTAssertThrowsError: nothing thrown", message(), file, line)
    } catch {
        handler(error)
    }
}

func XCTAssertNoThrow<T>(_ e: @autoclosure () throws -> T, _ message: @autoclosure () -> String = "", file: StaticString = #filePath, line: UInt = #line) {
    do {
        _ = try e()
    } catch {
        record("XCTAssertNoThrow: threw \(error)", message(), file, line)
    }
}
