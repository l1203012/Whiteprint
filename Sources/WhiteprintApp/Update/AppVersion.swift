import Foundation

/// A release version, MAJOR.MINOR.PATCH with an optional -prerelease, ordered
/// the semantic-versioning way: 1.0.0-beta.2 < 1.0.0-beta.10 < 1.0.0.
struct AppVersion: Comparable, CustomStringConvertible {
    let major: Int
    let minor: Int
    let patch: Int
    /// The dot-separated parts after the "-", empty for a final release.
    let prerelease: [String]

    /// Parses "1.2.3", "v1.2.3" or "1.2.3-beta.1"; nil for anything else.
    init?(_ text: String) {
        var text = Substring(text.trimmingCharacters(in: .whitespaces))
        if text.first == "v" { text = text.dropFirst() }
        let dash = text.firstIndex(of: "-")
        let core = dash.map { text[..<$0] } ?? text
        let numbers = core.split(separator: ".", omittingEmptySubsequences: false).map { Int($0) }
        guard numbers.count == 3, let major = numbers[0], let minor = numbers[1], let patch = numbers[2],
              major >= 0, minor >= 0, patch >= 0 else { return nil }
        let parts = dash.map { text[text.index(after: $0)...].split(separator: ".", omittingEmptySubsequences: false).map(String.init) } ?? []
        guard parts.allSatisfy({ !$0.isEmpty }) else { return nil }
        self.major = major
        self.minor = minor
        self.patch = patch
        prerelease = parts
    }

    var isPrerelease: Bool { !prerelease.isEmpty }

    /// An edge build of `main` (0.1.0-beta.1-edge.42), which never updates itself.
    var isEdge: Bool { description.contains("-edge") }

    var description: String {
        "\(major).\(minor).\(patch)" + (prerelease.isEmpty ? "" : "-" + prerelease.joined(separator: "."))
    }

    static func < (a: AppVersion, b: AppVersion) -> Bool {
        if (a.major, a.minor, a.patch) != (b.major, b.minor, b.patch) {
            return (a.major, a.minor, a.patch) < (b.major, b.minor, b.patch)
        }
        // A final release ranks above its prereleases.
        if a.prerelease.isEmpty || b.prerelease.isEmpty { return !a.prerelease.isEmpty && b.prerelease.isEmpty }
        for (x, y) in zip(a.prerelease, b.prerelease) where x != y {
            switch (Int(x), Int(y)) {
            case let (x?, y?): return x < y
            case (.some, nil): return true
            case (nil, .some): return false
            case (nil, nil): return x < y
            }
        }
        return a.prerelease.count < b.prerelease.count
    }

    static func == (a: AppVersion, b: AppVersion) -> Bool {
        (a.major, a.minor, a.patch) == (b.major, b.minor, b.patch) && a.prerelease == b.prerelease
    }
}
