import CryptoKit
import Foundation

/// A newer Whiteprint on GitHub Releases, with the DMG for this Mac.
struct AvailableUpdate: Equatable {
    let version: AppVersion
    let dmg: URL
    /// The base64 Ed25519 signature of the DMG, `<dmg>.sig` next to it.
    let signature: URL
    let size: Int
    /// The release page, for installing by hand.
    let page: URL
}

/// Reads the GitHub Releases list and picks the update for this Mac.
enum UpdateFeed {
    struct Release: Decodable {
        struct Asset: Decodable {
            let name: String
            let browserDownloadUrl: URL
            let size: Int
        }
        let tagName: String
        let draft: Bool
        let prerelease: Bool
        let htmlUrl: URL
        let assets: [Asset]
    }

    enum Architecture: String {
        case appleSilicon = "AppleSilicon"
        case intel = "Intel"

        /// The Mac's chip, even when this copy runs under Rosetta.
        static var current: Architecture {
            var arm64: Int32 = 0
            var size = MemoryLayout<Int32>.size
            return sysctlbyname("hw.optional.arm64", &arm64, &size, nil, 0) == 0 && arm64 == 1 ? .appleSilicon : .intel
        }
    }

    static func releasesURL(repository: String) -> URL? {
        URL(string: "https://api.github.com/repos/\(repository)/releases?per_page=20")
    }

    /// The newest release above `current` that has a signed DMG for `architecture`.
    /// Pre-releases count only while `current` is one, so a beta tester keeps
    /// getting betas and someone on a final release only gets final releases.
    static func pick(from releases: [Release], current: AppVersion, architecture: Architecture) -> AvailableUpdate? {
        releases.compactMap { release -> AvailableUpdate? in
            guard !release.draft, release.tagName.hasPrefix("v"),
                  let version = AppVersion(release.tagName), version > current,
                  current.isPrerelease || !(release.prerelease || version.isPrerelease) else { return nil }
            let name = "Whiteprint-\(version)-\(architecture.rawValue).dmg"
            guard let dmg = release.assets.first(where: { $0.name == name }),
                  let signature = release.assets.first(where: { $0.name == name + ".sig" }) else { return nil }
            return AvailableUpdate(version: version, dmg: dmg.browserDownloadUrl, signature: signature.browserDownloadUrl,
                                   size: dmg.size, page: release.htmlUrl)
        }
        .max { $0.version < $1.version }
    }

    /// Whether `signature` (base64) is `publicKey`'s (base64, raw 32 bytes) Ed25519 signature of `data`.
    static func isSigned(_ data: Data, signature: String, publicKey: String) -> Bool {
        guard let signature = Data(base64Encoded: signature.trimmingCharacters(in: .whitespacesAndNewlines)),
              let keyData = Data(base64Encoded: publicKey),
              let key = try? Curve25519.Signing.PublicKey(rawRepresentation: keyData) else { return false }
        return key.isValidSignature(signature, for: data)
    }
}
