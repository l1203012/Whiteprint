import Foundation
import WhiteprintEditor
import WhiteprintRender

extension Notification.Name {
    /// Posted when a view preference changes; every open editor applies it.
    static let viewPreferencesDidChange = Notification.Name("WhiteprintViewPreferencesDidChange")
}

/// App-wide editor display options from the View menu and the Touch Bar.
final class ViewPreferences {
    static let shared = ViewPreferences(defaults: AppDefaults.store)

    static let layoutKey = "PageLayout"
    static let syntaxKey = "ShowMarkdownSyntax"
    static let themeKey = "PageTheme"

    private let defaults: UserDefaults

    init(defaults: UserDefaults) {
        self.defaults = defaults
    }

    var layoutMode: PageLayoutMode {
        get { defaults.string(forKey: Self.layoutKey).flatMap(PageLayoutMode.init(rawValue:)) ?? .slides }
        set {
            guard newValue != layoutMode else { return }
            defaults.set(newValue.rawValue, forKey: Self.layoutKey)
            changed()
        }
    }

    var showsMarkdownSyntax: Bool {
        get { defaults.bool(forKey: Self.syntaxKey) }
        set {
            guard newValue != showsMarkdownSyntax else { return }
            defaults.set(newValue, forKey: Self.syntaxKey)
            changed()
        }
    }

    var pageTheme: PageTheme {
        get { defaults.string(forKey: Self.themeKey).flatMap(PageTheme.init(rawValue:)) ?? .default }
        set {
            guard newValue != pageTheme else { return }
            defaults.set(newValue.rawValue, forKey: Self.themeKey)
            changed()
        }
    }

    private func changed() {
        NotificationCenter.default.post(name: .viewPreferencesDidChange, object: self)
    }
}
