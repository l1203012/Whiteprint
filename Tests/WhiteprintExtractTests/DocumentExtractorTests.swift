import Foundation
import XCTest
@testable import WhiteprintExtract

final class DocumentExtractorTests: XCTestCase {
    func testRejectsUnsupportedExtensions() {
        for name in ["notes.txt", "sheet.xlsx", "deck.key", "noextension"] {
            XCTAssertThrowsError(try DocumentExtractor.extract(URL(fileURLWithPath: "/tmp/\(name)"))) {
                XCTAssertEqual($0 as? ExtractionError, .unsupportedType(name))
            }
        }
    }

    func testExtensionsAreCaseInsensitive() throws {
        let missing = URL(fileURLWithPath: "/tmp/does-not-exist-\(UUID().uuidString).PDF")
        XCTAssertThrowsError(try DocumentExtractor.extract(missing)) {
            XCTAssertEqual($0 as? ExtractionError, .unreadable(missing.lastPathComponent))
        }
    }
}
