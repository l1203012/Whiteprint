import AppKit
import WhiteprintEditor
import WhiteprintBridge
import WhiteprintCore
import WhiteprintRender
import WhiteprintStudy

/// App lifecycle: menus, the bridge server, the notes folder and the
/// app-wide actions (new note, palette, study panel, settings).
final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate, NSMenuItemValidation {
    private var server: BridgeServer?
    private var settings: SettingsWindowController?
    private var reference: NSWindowController?

    func applicationWillFinishLaunching(_ notification: Notification) {
        NSApp.mainMenu = MainMenu.make(recentDelegate: self)
        NSApp.isAutomaticCustomizeTouchBarMenuItemEnabled = true
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        ScreenshotMode.startIfRequested()
        let services = AppServices.shared
        let server = BridgeServer(handler: services.bridge)
        do {
            try server.start()
            self.server = server
        } catch {
            NSLog("Whiteprint: couldn't start the Claude bridge: \(error)")
        }
        // Documents restored or opened from Finder arrive during launch;
        // only show something ourselves when nothing else opened.
        DispatchQueue.main.async {
            guard NoteDocuments.open.isEmpty else { return }
            self.openStartupNote()
        }    }

    func applicationWillTerminate(_ notification: Notification) {
        server?.stop()
        AppServices.shared.study.cancel()
    }

    func applicationShouldOpenUntitledFile(_ sender: NSApplication) -> Bool {
        false
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows: Bool) -> Bool {
        if !hasVisibleWindows { openStartupNote() }
        return false
    }

    /// The most recently edited note, or a new Welcome note in an empty folder.
    private func openStartupNote() {
        let library = AppServices.shared.library
        let urls = library.folder.noteURLs()
        if urls.isEmpty {
            do {
                NoteDocuments.open(try library.folder.save(WelcomeNote.note, title: WelcomeNote.title))
                library.reload()
            } catch {
                NSApp.presentError(error)
            }
            return
        }
        let modified = { (url: URL) in
            (try? url.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
        }
        if let newest = urls.max(by: { modified($0) < modified($1) }) {
            NoteDocuments.open(newest)
        }
    }

    // MARK: Actions

    /// A new note in a new tab, in the folder selected in the front window's sidebar.
    @IBAction func newNote(_ sender: Any?) {
        newNote(in: (NoteDocuments.frontWindow?.windowController as? NoteWindowController)?.selectedFolder)
    }

    func newNote(in folder: URL?) {
        let library = AppServices.shared.library
        do {
            let url = try library.folder.save(Note(), title: NoteTitle.untitled, in: folder)
            library.reload()
            NoteDocuments.open(url)
        } catch {
            NSApp.presentError(error)
        }
    }

    @IBAction func newFolder(_ sender: Any?) {
        if let controller = NoteDocuments.frontWindow?.windowController as? NoteWindowController {
            controller.newFolder(sender)
        } else {
            createFolder(in: nil)
        }
    }

    @discardableResult
    func createFolder(in parent: URL?) -> URL? {
        let library = AppServices.shared.library
        do {
            let url = try library.folder.createFolder(in: parent)
            library.reload()
            return url
        } catch {
            NSApp.presentError(error)
            return nil
        }
    }

    @IBAction func studyFlashcards(_ sender: Any?) {
        if let controller = NoteDocuments.frontWindow?.windowController as? NoteWindowController {
            controller.studyFlashcards(sender)
        } else {
            CommandPalette.shared.showDecks(over: nil)
        }
    }

    @IBAction func setPageLayout(_ sender: Any?) {
        guard let raw = (sender as? NSMenuItem)?.representedObject as? String,
              let mode = PageLayoutMode(rawValue: raw) else { return }
        ViewPreferences.shared.layoutMode = mode
    }

    @IBAction func setPageTheme(_ sender: Any?) {
        guard let raw = (sender as? NSMenuItem)?.representedObject as? String,
              let theme = PageTheme(rawValue: raw) else { return }
        ViewPreferences.shared.pageTheme = theme
    }

    @IBAction func toggleMarkdownSyntax(_ sender: Any?) {
        ViewPreferences.shared.showsMarkdownSyntax.toggle()
    }

    func validateMenuItem(_ item: NSMenuItem) -> Bool {
        switch item.action {
        case #selector(setPageLayout(_:)):
            item.state = item.representedObject as? String == ViewPreferences.shared.layoutMode.rawValue ? .on : .off
        case #selector(setPageTheme(_:)):
            item.state = item.representedObject as? String == ViewPreferences.shared.pageTheme.rawValue ? .on : .off
        case #selector(toggleMarkdownSyntax(_:)):
            item.state = ViewPreferences.shared.showsMarkdownSyntax ? .on : .off
        default:
            break
        }
        return true
    }

    @IBAction func showCommandPalette(_ sender: Any?) {
        CommandPalette.shared.show(over: NoteDocuments.frontWindow)
    }

    @IBAction func showStudyPanel(_ sender: Any?) {
        StudyPanelController.present(over: NoteDocuments.frontWindow)
    }

    @IBAction func generateStudyPlan(_ sender: Any?) {
        StudyPanelController.present(over: NoteDocuments.frontWindow, generating: true)
    }

    @IBAction func showSettings(_ sender: Any?) {
        if settings == nil { settings = SettingsWindowController() }
        settings?.showWindow(sender)
        settings?.window?.makeKeyAndOrderFront(sender)
    }

    @IBAction func showDrawingReference(_ sender: Any?) {
        if reference == nil {
            reference = ReferenceWindow.make(title: "Drawing Language", text: WhiteprintText.dslReference)
        }
        reference?.showWindow(sender)
    }

    // MARK: Open Recent

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        for url in NSDocumentController.shared.recentDocumentURLs {
            let item = NSMenuItem(title: NoteTitle.baseName(url), action: #selector(openRecent(_:)), keyEquivalent: "")
            item.representedObject = url
            item.target = self
            item.image = NSWorkspace.shared.icon(forFile: url.path)
            item.image?.size = NSSize(width: 16, height: 16)
            menu.addItem(item)
        }
        if menu.items.isEmpty {
            menu.addItem(NSMenuItem(title: "No Recent Notes", action: nil, keyEquivalent: ""))
        }
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "Clear Menu", action: #selector(NSDocumentController.clearRecentDocuments(_:)), keyEquivalent: ""))
    }

    @objc private func openRecent(_ sender: NSMenuItem) {
        (sender.representedObject as? URL).map(NoteDocuments.open)
    }
}
