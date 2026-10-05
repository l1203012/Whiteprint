import Foundation

/// The user defaults Whiteprint's own preferences live in.
enum AppDefaults {
    /// Names a separate defaults suite, so a trial run doesn't touch real preferences.
    static let suiteEnvironmentKey = "WHITEPRINT_DEFAULTS_SUITE"

    static let store: UserDefaults = {
        if let suite = ProcessInfo.processInfo.environment[suiteEnvironmentKey], !suite.isEmpty,
           let defaults = UserDefaults(suiteName: suite) {
            return defaults
        }
        return .standard
    }()
}
