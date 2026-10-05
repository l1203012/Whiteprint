import Foundation
import WhiteprintBridge
import WhiteprintStudy

/// Which AI builds study plans.
enum AIProvider: String, CaseIterable {
    case claudeCode, grok

    var title: String {
        switch self {
        case .claudeCode: return "Claude Code (your subscription)"
        case .grok: return "Grok (xAI API key)"
        }
    }

    var name: String {
        switch self {
        case .claudeCode: return "Claude Code"
        case .grok: return "Grok"
        }
    }
}

/// The AI provider choice and the Grok model live in user defaults; the
/// xAI API key only ever lives in the Keychain.
final class AISettings {
    static let shared = AISettings(defaults: AppDefaults.store, keychainService: Bundle.main.bundleIdentifier ?? BridgePaths.appBundleID)

    static let providerKey = "AIProvider"
    static let grokModelKey = "GrokModel"

    private let defaults: UserDefaults
    let apiKeyItem: KeychainItem

    init(defaults: UserDefaults, keychainService: String) {
        self.defaults = defaults
        apiKeyItem = KeychainItem(service: keychainService, account: "xai-api-key")
    }

    var provider: AIProvider {
        get { defaults.string(forKey: Self.providerKey).flatMap(AIProvider.init(rawValue:)) ?? .claudeCode }
        set { defaults.set(newValue.rawValue, forKey: Self.providerKey) }
    }

    var grokModel: String {
        get {
            let model = defaults.string(forKey: Self.grokModelKey)?.trimmingCharacters(in: .whitespaces) ?? ""
            return model.isEmpty ? GrokRunner.Configuration.defaultModel : model
        }
        set {
            let model = newValue.trimmingCharacters(in: .whitespaces)
            if model.isEmpty || model == GrokRunner.Configuration.defaultModel {
                defaults.removeObject(forKey: Self.grokModelKey)
            } else {
                defaults.set(model, forKey: Self.grokModelKey)
            }
        }
    }

    /// The configuration for a Grok run, or nil without an API key.
    var grokConfiguration: GrokRunner.Configuration? {
        guard let key = apiKeyItem.read()?.trimmingCharacters(in: .whitespacesAndNewlines), !key.isEmpty else { return nil }
        return GrokRunner.Configuration(apiKey: key, model: grokModel)
    }
}
