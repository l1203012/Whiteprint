import CryptoKit
import Foundation
import XCTest
@testable import WhiteprintApp

final class AppVersionTests: XCTestCase {
    func testParsing() throws {
        XCTAssertEqual(AppVersion("1.2.3")?.description, "1.2.3")
        XCTAssertEqual(AppVersion("v0.1.0-beta.1")?.description, "0.1.0-beta.1")
        XCTAssertEqual(AppVersion("0.1.0-beta.1")?.prerelease, ["beta", "1"])
        for bad in ["", "1.2", "1.2.3.4", "1.x.3", "1.2.3-", "1.2.3-beta..1", "windows-v0.1.1"] {
            XCTAssertNil(AppVersion(bad), bad)
        }
        XCTAssertTrue(try XCTUnwrap(AppVersion("0.1.0-beta.1-edge.42")).isEdge)
        XCTAssertFalse(try XCTUnwrap(AppVersion("0.1.0-beta.1")).isEdge)
    }

    func testOrdering() throws {
        let ordered = ["0.1.0-alpha", "0.1.0-alpha.1", "0.1.0-beta.1", "0.1.0-beta.2", "0.1.0-beta.10",
                       "0.1.0-rc.1", "0.1.0", "0.1.1", "0.2.0", "1.0.0"]
        let versions = try ordered.map { try XCTUnwrap(AppVersion($0)) }
        for (a, b) in zip(versions, versions.dropFirst()) {
            XCTAssertLessThan(a, b, "\(a) < \(b)")
            XCTAssertFalse(b < a, "\(b) < \(a)")
        }
        XCTAssertEqual(AppVersion("v1.0.0"), AppVersion("1.0.0"))
    }
}

final class UpdateFeedTests: XCTestCase {
    private func release(_ tag: String, prerelease: Bool = false, draft: Bool = false, signed: Bool = true,
                         kinds: [String] = ["AppleSilicon", "Intel"]) -> String {
        let version = tag.hasPrefix("v") ? String(tag.dropFirst()) : tag
        let assets = kinds.flatMap { kind -> [String] in
            let name = "Whiteprint-\(version)-\(kind).dmg"
            let dmg = #"{"name": "\#(name)", "browser_download_url": "https://example.com/\#(name)", "size": 100}"#
            let sig = #"{"name": "\#(name).sig", "browser_download_url": "https://example.com/\#(name).sig", "size": 88}"#
            return signed ? [dmg, sig] : [dmg]
        }
        return """
        {"tag_name": "\(tag)", "draft": \(draft), "prerelease": \(prerelease),
         "html_url": "https://example.com/\(tag)", "assets": [\(assets.joined(separator: ","))]}
        """
    }

    private func pick(_ releases: [String], current: String,
                      architecture: UpdateFeed.Architecture = .appleSilicon) throws -> String? {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        let parsed = try decoder.decode([UpdateFeed.Release].self, from: Data("[\(releases.joined(separator: ","))]".utf8))
        return UpdateFeed.pick(from: parsed, current: try XCTUnwrap(AppVersion(current)), architecture: architecture)?.version.description
    }

    func testPicksNewestNewerRelease() throws {
        let releases = [release("v0.1.0"), release("v0.3.0"), release("v0.2.0")]
        XCTAssertEqual(try pick(releases, current: "0.1.0"), "0.3.0")
        XCTAssertNil(try pick(releases, current: "0.3.0"))
        XCTAssertNil(try pick(releases, current: "1.0.0"))
    }

    func testPrereleasesOnlyForPrereleaseUsers() throws {
        let releases = [release("v0.1.0"), release("v0.2.0-beta.1", prerelease: true)]
        XCTAssertEqual(try pick(releases, current: "0.1.0-beta.1"), "0.2.0-beta.1")
        XCTAssertNil(try pick(releases, current: "0.1.0"))
    }

    func testSkipsDraftsUnsignedWindowsAndOtherArchitectures() throws {
        XCTAssertNil(try pick([release("v0.2.0", draft: true)], current: "0.1.0"))
        XCTAssertNil(try pick([release("v0.2.0", signed: false)], current: "0.1.0"))
        XCTAssertNil(try pick([release("windows-v0.2.0")], current: "0.1.0"))
        XCTAssertNil(try pick([release("v0.2.0", kinds: ["Intel"])], current: "0.1.0", architecture: .appleSilicon))
        XCTAssertEqual(try pick([release("v0.2.0", kinds: ["Intel"])], current: "0.1.0", architecture: .intel), "0.2.0")
    }

    func testSignatureCheck() {
        let key = Curve25519.Signing.PrivateKey()
        let publicKey = key.publicKey.rawRepresentation.base64EncodedString()
        let data = Data("Whiteprint-0.2.0-AppleSilicon.dmg".utf8)
        let signature = (try? key.signature(for: data))?.base64EncodedString() ?? ""
        XCTAssertTrue(UpdateFeed.isSigned(data, signature: signature + "\n", publicKey: publicKey))
        XCTAssertFalse(UpdateFeed.isSigned(data + Data([0]), signature: signature, publicKey: publicKey))
        let other = Curve25519.Signing.PrivateKey().publicKey.rawRepresentation.base64EncodedString()
        XCTAssertFalse(UpdateFeed.isSigned(data, signature: signature, publicKey: other))
        XCTAssertFalse(UpdateFeed.isSigned(data, signature: "not base64", publicKey: publicKey))
    }
}
