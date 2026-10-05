import Foundation
import XCTest
@testable import WhiteprintExtract

final class ZipArchiveTests: XCTestCase {
    private var directory: URL!

    override func setUpWithError() throws {
        directory = try Fixtures.temporaryDirectory()
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: directory)
    }

    func testReadsDeflatedAndStoredEntries() throws {
        let long = String(repeating: "Repetitive text compresses well. ", count: 200)
        let url = directory.appendingPathComponent("mixed.zip")
        try Fixtures.writeZip(["a/long.txt": long, "b/plain.txt": "stored as is", "empty.txt": ""],
                              stored: ["b/plain.txt"], to: url)

        let archive = try ZipArchive(url: url)
        XCTAssertEqual(archive.entry(named: "a/long.txt")?.method, 8)
        XCTAssertEqual(archive.entry(named: "b/plain.txt")?.method, 0)
        XCTAssertEqual(try archive.contents(of: "a/long.txt"), Data(long.utf8))
        XCTAssertEqual(try archive.contents(of: "b/plain.txt"), Data("stored as is".utf8))
        XCTAssertEqual(try archive.contents(of: "empty.txt"), Data())
        XCTAssertNil(try archive.contents(of: "missing.txt"))
    }

    func testHandBuiltStoredArchive() throws {
        let archive = try ZipArchive(data: Fixtures.storedZip(name: "x.xml", contents: Data("<x/>".utf8)))
        XCTAssertEqual(archive.entries.map(\.name), ["x.xml"])
        XCTAssertEqual(try archive.contents(of: "x.xml"), Data("<x/>".utf8))
    }

    func testRejectsDataThatIsNotAZip() {
        XCTAssertThrowsError(try ZipArchive(data: Data("not a zip at all, just some text".utf8))) {
            XCTAssertEqual($0 as? ZipArchive.Failure, .notAZip)
        }
        XCTAssertThrowsError(try ZipArchive(data: Data()))
    }

    func testRejectsTruncatedArchive() throws {
        let data = Fixtures.storedZip(name: "x.xml", contents: Data(String(repeating: "x", count: 100).utf8))
        // Dropping the start keeps the end record but its offsets now point past the data.
        XCTAssertThrowsError(try ZipArchive(data: data.dropFirst(60)).contents(of: "x.xml"))
    }

    func testRejectsAbsurdUncompressedSize() throws {
        var data = Fixtures.storedZip(name: "bomb.xml", contents: Data("tiny".utf8))
        let directory = data.count - 22 - (46 + 8)
        // Uncompressed size in the central directory: 3 GB.
        data.replaceSubrange(directory + 24 ..< directory + 28, with: [0x00, 0x00, 0x00, 0xC0])
        let archive = try ZipArchive(data: data)
        XCTAssertThrowsError(try archive.contents(of: "bomb.xml")) {
            XCTAssertEqual($0 as? ZipArchive.Failure, .tooLarge("bomb.xml"))
        }
    }

    func testDetectsChecksumMismatch() throws {
        var data = Fixtures.storedZip(name: "x.txt", contents: Data("hello".utf8))
        data[30 + 5] = UInt8(ascii: "j")
        XCTAssertThrowsError(try ZipArchive(data: data).contents(of: "x.txt")) {
            XCTAssertEqual($0 as? ZipArchive.Failure, .corrupt("checksum mismatch for x.txt"))
        }
    }

    func testDetectsCorruptDeflateStream() throws {
        let url = directory.appendingPathComponent("deflated.zip")
        let text = String(repeating: "abcdefghij", count: 500)
        try Fixtures.writeZip(["t.txt": text], to: url)
        var data = try Data(contentsOf: url)
        let start = 30 + "t.txt".count
        for i in start ..< start + 8 { data[i] ^= 0xFF }
        XCTAssertThrowsError(try ZipArchive(data: data).contents(of: "t.txt"))
    }

    func testCRC32MatchesKnownValue() {
        XCTAssertEqual(CRC32.checksum(Data("123456789".utf8)), 0xCBF4_3926)
    }

    func testResolvesPackagePaths() {
        XCTAssertEqual(PackagePath.resolve("../notesSlides/notesSlide3.xml", relativeTo: "ppt/slides/slide3.xml"),
                       "ppt/notesSlides/notesSlide3.xml")
        XCTAssertEqual(PackagePath.resolve("slides/slide1.xml", relativeTo: "ppt/presentation.xml"), "ppt/slides/slide1.xml")
        XCTAssertEqual(PackagePath.resolve("/ppt/slides/./slide2.xml", relativeTo: "ppt/presentation.xml"), "ppt/slides/slide2.xml")
    }
}
