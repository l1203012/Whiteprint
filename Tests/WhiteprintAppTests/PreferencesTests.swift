import AppKit
import XCTest
@testable import WhiteprintApp
import WhiteprintRender
import WhiteprintStudy

final class ViewPreferencesTests: XCTestCase {
    private var suite: String!
    private var defaults: UserDefaults!

    override func setUpWithError() throws {
        suite = "wp-tests-\(UUID().uuidString)"
        defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
    }

    override func tearDown() {
        defaults.removePersistentDomain(forName: suite)
    }

    func testDefaultsAndChanges() {
        let preferences = ViewPreferences(defaults: defaults)
        XCTAssertEqual(preferences.layoutMode, .slides)
        XCTAssertFalse(preferences.showsMarkdownSyntax)
        var posts = 0
        let observer = NotificationCenter.default.addObserver(forName: .viewPreferencesDidChange, object: preferences, queue: nil) { _ in posts += 1 }
        defer { NotificationCenter.default.removeObserver(observer) }
        preferences.layoutMode = .a4
        preferences.layoutMode = .a4
        preferences.showsMarkdownSyntax = true
        XCTAssertEqual(posts, 2)
        XCTAssertEqual(ViewPreferences(defaults: defaults).layoutMode, .a4)
        XCTAssertTrue(ViewPreferences(defaults: defaults).showsMarkdownSyntax)
    }

    func testPageThemeDefaultsToPaper() {
        let preferences = ViewPreferences(defaults: defaults)
        XCTAssertEqual(preferences.pageTheme, .paper)
        var posts = 0
        let observer = NotificationCenter.default.addObserver(forName: .viewPreferencesDidChange, object: preferences, queue: nil) { _ in posts += 1 }
        defer { NotificationCenter.default.removeObserver(observer) }
        preferences.pageTheme = .blueprint
        preferences.pageTheme = .blueprint
        XCTAssertEqual(posts, 1)
        XCTAssertEqual(ViewPreferences(defaults: defaults).pageTheme, .blueprint)
        defaults.set("plaid", forKey: ViewPreferences.themeKey)
        XCTAssertEqual(ViewPreferences(defaults: defaults).pageTheme, .paper, "an unknown theme falls back to the default")
    }

    func testTouchBarItems() throws {
        let preferences = ViewPreferences(defaults: defaults)
        preferences.layoutMode = .a4
        let provider = NoteTouchBar(preferences: preferences)
        let bar = provider.makeTouchBar()
        XCTAssertEqual(bar.customizationIdentifier, NoteTouchBar.customizationID)
        XCTAssertTrue(bar.defaultItemIdentifiers.contains(.otherItemsProxy))
        for id in [NoteTouchBar.Item.newNote, NoteTouchBar.Item.palette, NoteTouchBar.Item.sidebar, NoteTouchBar.Item.study] {
            let item = try XCTUnwrap(bar.item(forIdentifier: id) as? NSButtonTouchBarItem, id.rawValue)
            XCTAssertNotNil(item.action)
            XCTAssertFalse(item.customizationLabel.isEmpty)
            XCTAssertTrue(bar.customizationAllowedItemIdentifiers.contains(id))
        }
        let layout = try XCTUnwrap((bar.item(forIdentifier: NoteTouchBar.Item.layout) as? NSCustomTouchBarItem)?.view as? NSSegmentedControl)
        XCTAssertEqual(layout.segmentCount, 2)
        XCTAssertEqual(layout.selectedSegment, 1)
        let syntax = try XCTUnwrap((bar.item(forIdentifier: NoteTouchBar.Item.syntax) as? NSCustomTouchBarItem)?.view as? NSButton)
        XCTAssertEqual(syntax.state, .off)

        layout.selectedSegment = 0
        _ = (layout.target as? NSObject)?.perform(layout.action, with: layout)
        XCTAssertEqual(preferences.layoutMode, .slides)
        syntax.state = .on
        _ = (syntax.target as? NSObject)?.perform(syntax.action, with: syntax)
        XCTAssertTrue(preferences.showsMarkdownSyntax)
        preferences.layoutMode = .a4
        XCTAssertEqual(layout.selectedSegment, 1)
    }
}

final class AISettingsTests: XCTestCase {
    private var suite: String!
    private var defaults: UserDefaults!
    private let service = "io.github.l1203012.whiteprint.tests.\(UUID().uuidString)"

    override func setUpWithError() throws {
        suite = "wp-tests-\(UUID().uuidString)"
        defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
    }

    override func tearDown() {
        defaults.removePersistentDomain(forName: suite)
        try? KeychainItem(service: service, account: "xai-api-key").delete()
    }

    func testProviderAndModel() {
        let settings = AISettings(defaults: defaults, keychainService: service)
        XCTAssertEqual(settings.provider, .claudeCode)
        XCTAssertEqual(settings.grokModel, GrokRunner.Configuration.defaultModel)
        settings.provider = .grok
        settings.grokModel = "  grok-4-fast "
        let reloaded = AISettings(defaults: defaults, keychainService: service)
        XCTAssertEqual(reloaded.provider, .grok)
        XCTAssertEqual(reloaded.grokModel, "grok-4-fast")
        reloaded.grokModel = ""
        XCTAssertEqual(reloaded.grokModel, GrokRunner.Configuration.defaultModel)
    }

    func testKeyLivesInTheKeychainOnly() throws {
        let settings = AISettings(defaults: defaults, keychainService: service)
        XCTAssertNil(settings.grokConfiguration)
        try settings.apiKeyItem.save("xai-first")
        try settings.apiKeyItem.save(" xai-second\n")
        XCTAssertEqual(settings.apiKeyItem.read(), " xai-second\n")
        XCTAssertEqual(settings.grokConfiguration, GrokRunner.Configuration(apiKey: "xai-second", model: GrokRunner.Configuration.defaultModel))
        let stored = defaults.dictionaryRepresentation().values.compactMap { $0 as? String }
        XCTAssertFalse(stored.contains { $0.contains("xai-second") })
        try settings.apiKeyItem.delete()
        XCTAssertNil(settings.apiKeyItem.read())
        XCTAssertNil(settings.grokConfiguration)
        try settings.apiKeyItem.delete()
    }
}
