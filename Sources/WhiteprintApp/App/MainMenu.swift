import AppKit
import WhiteprintEditor
import WhiteprintRender

/// The menu bar, built in code. Actions go up the responder chain: note
/// actions to the window controller or document, app actions to the delegate.
enum MainMenu {
    static func make(recentDelegate: NSMenuDelegate) -> NSMenu {
        let main = NSMenu()
        main.addItem(submenu(appMenu()))
        main.addItem(submenu(fileMenu(recentDelegate: recentDelegate)))
        main.addItem(submenu(editMenu()))
        main.addItem(submenu(viewMenu()))
        main.addItem(submenu(formatMenu()))
        main.addItem(submenu(noteMenu()))
        let window = windowMenu()
        main.addItem(submenu(window))
        NSApp.windowsMenu = window
        let help = helpMenu()
        main.addItem(submenu(help))
        NSApp.helpMenu = help
        return main
    }

    private static func appMenu() -> NSMenu {
        let name = ProcessInfo.processInfo.processName
        let menu = NSMenu(title: name)
        menu.addItem(item("About \(name)", #selector(NSApplication.orderFrontStandardAboutPanel(_:))))
        menu.addItem(item("Check for Updates…", #selector(AppDelegate.checkForUpdates(_:))))
        menu.addItem(.separator())
        menu.addItem(item("Settings…", #selector(AppDelegate.showSettings(_:)), ","))
        menu.addItem(.separator())
        let services = NSMenu(title: "Services")
        menu.addItem(submenu(services))
        NSApp.servicesMenu = services
        menu.addItem(.separator())
        menu.addItem(item("Hide \(name)", #selector(NSApplication.hide(_:)), "h"))
        menu.addItem(item("Hide Others", #selector(NSApplication.hideOtherApplications(_:)), "h", [.command, .option]))
        menu.addItem(item("Show All", #selector(NSApplication.unhideAllApplications(_:))))
        menu.addItem(.separator())
        menu.addItem(item("Quit \(name)", #selector(NSApplication.terminate(_:)), "q"))
        return menu
    }

    private static func fileMenu(recentDelegate: NSMenuDelegate) -> NSMenu {
        let menu = NSMenu(title: "File")
        menu.addItem(item("New Note", #selector(AppDelegate.newNote(_:)), "n"))
        menu.addItem(item("New Tab", #selector(NSResponder.newWindowForTab(_:)), "t"))
        menu.addItem(item("New Folder", #selector(AppDelegate.newFolder(_:)), "n", [.command, .shift]))
        menu.addItem(item("Open…", #selector(NSDocumentController.openDocument(_:)), "o"))
        let recent = NSMenu(title: "Open Recent")
        recent.delegate = recentDelegate
        menu.addItem(submenu(recent))
        menu.addItem(.separator())
        menu.addItem(item("Close", #selector(NSWindow.performClose(_:)), "w"))
        menu.addItem(item("Save", #selector(NSDocument.save(_:)), "s"))
        menu.addItem(item("Duplicate", #selector(NSDocument.duplicate(_:)), "s", [.command, .shift]))
        menu.addItem(item("Rename…", #selector(NSDocument.rename(_:))))
        menu.addItem(item("Move To…", #selector(NSDocument.move(_:))))
        menu.addItem(item("Revert To Saved", #selector(NSDocument.revertToSaved(_:))))
        menu.addItem(.separator())
        let export = NSMenu(title: "Export")
        export.addItem(item("Markdown…", #selector(NoteDocument.exportMarkdown(_:))))
        export.addItem(item("PDF (Blueprint)…", #selector(NoteDocument.exportBlueprintPDF(_:))))
        export.addItem(item("PDF (Print)…", #selector(NoteDocument.exportPrintPDF(_:))))
        menu.addItem(submenu(export))
        menu.addItem(item("Import for Study Plan…", #selector(AppDelegate.showStudyPanel(_:))))
        menu.addItem(.separator())
        menu.addItem(item("Show in Finder", #selector(NoteDocument.showInFinder(_:)), "r", [.command, .option]))
        return menu
    }

    private static func editMenu() -> NSMenu {
        let menu = NSMenu(title: "Edit")
        menu.addItem(item("Undo", Selector(("undo:")), "z"))
        menu.addItem(item("Redo", Selector(("redo:")), "z", [.command, .shift]))
        menu.addItem(.separator())
        menu.addItem(item("Cut", #selector(NSText.cut(_:)), "x"))
        menu.addItem(item("Copy", #selector(NSText.copy(_:)), "c"))
        menu.addItem(item("Paste", #selector(NSText.paste(_:)), "v"))
        menu.addItem(item("Paste and Match Style", #selector(NSTextView.pasteAsPlainText(_:)), "v", [.command, .option, .shift]))
        menu.addItem(item("Delete", #selector(NSText.delete(_:))))
        menu.addItem(item("Select All", #selector(NSText.selectAll(_:)), "a"))
        menu.addItem(.separator())
        let find = NSMenu(title: "Find")
        find.addItem(findItem("Find…", .showFindInterface, "f"))
        find.addItem(findItem("Find and Replace…", .showReplaceInterface, "f", [.command, .option]))
        find.addItem(findItem("Find Next", .nextMatch, "g"))
        find.addItem(findItem("Find Previous", .previousMatch, "g", [.command, .shift]))
        find.addItem(findItem("Use Selection for Find", .setSearchString, "e"))
        find.addItem(item("Jump to Selection", #selector(NSResponder.centerSelectionInVisibleArea(_:)), "j"))
        menu.addItem(submenu(find))
        let spelling = NSMenu(title: "Spelling and Grammar")
        spelling.addItem(item("Check Document Now", #selector(NSText.checkSpelling(_:)), ";"))
        spelling.addItem(item("Check Spelling While Typing", #selector(NSTextView.toggleContinuousSpellChecking(_:))))
        menu.addItem(submenu(spelling))
        return menu
    }

    private static func viewMenu() -> NSMenu {
        let menu = NSMenu(title: "View")
        menu.addItem(item("Toggle Sidebar", #selector(NSSplitViewController.toggleSidebar(_:)), "\\"))
        menu.addItem(item("Command Palette", #selector(AppDelegate.showCommandPalette(_:)), "k"))
        menu.addItem(.separator())
        let layout = NSMenu(title: "Page Layout")
        for mode in PageLayoutMode.allCases {
            let option = item(NoteTouchBar.title(mode), #selector(AppDelegate.setPageLayout(_:)))
            option.representedObject = mode.rawValue
            layout.addItem(option)
        }
        menu.addItem(submenu(layout))
        let theme = NSMenu(title: "Page Theme")
        for pageTheme in PageTheme.allCases {
            let option = item(pageTheme.title, #selector(AppDelegate.setPageTheme(_:)))
            option.representedObject = pageTheme.rawValue
            theme.addItem(option)
        }
        menu.addItem(submenu(theme))
        menu.addItem(item("Show Markdown Syntax", #selector(AppDelegate.toggleMarkdownSyntax(_:)), "m", [.command, .shift]))
        menu.addItem(.separator())
        menu.addItem(item("Enter Full Screen", #selector(NSWindow.toggleFullScreen(_:)), "f", [.command, .control]))
        return menu
    }

    private static func noteMenu() -> NSMenu {
        let menu = NSMenu(title: "Note")
        menu.addItem(item("Add Page", #selector(NoteWindowController.addPage(_:)), "n", [.command, .option]))
        menu.addItem(item("Insert Drawing", #selector(NoteWindowController.insertDrawing(_:)), "d", [.command, .shift]))
        menu.addItem(item("Insert Flashcards", #selector(NoteWindowController.insertFlashcards(_:)), "f", [.command, .shift]))
        menu.addItem(.separator())
        menu.addItem(item("Study Flashcards…", #selector(AppDelegate.studyFlashcards(_:))))
        menu.addItem(item("Generate Study Plan…", #selector(AppDelegate.generateStudyPlan(_:))))
        return menu
    }

    /// Text formatting, sent to the editor as `EditorCommand`s.
    private static func formatMenu() -> NSMenu {
        let menu = NSMenu(title: "Format")
        let groups: [[(String, EditorCommand, String, NSEvent.ModifierFlags)]] = [
            [
                ("Heading 1", .heading1, "1", [.command, .option]),
                ("Heading 2", .heading2, "2", [.command, .option]),
                ("Heading 3", .heading3, "3", [.command, .option]),
                ("Body", .body, "0", [.command, .option]),
            ],
            [("Bold", .bold, "b", .command), ("Italic", .italic, "i", .command), ("Inline Code", .inlineCode, "", .command)],
            [
                ("Bulleted List", .bulletList, "8", [.command, .option]),
                ("Numbered List", .numberedList, "7", [.command, .option]),
                ("Checklist", .checklist, "9", [.command, .option]),
                ("Quote", .quote, "", .command),
                ("Code Block", .codeBlock, "", .command),
                ("Divider", .divider, "", .command),
            ],
        ]
        for (index, group) in groups.enumerated() {
            if index > 0 { menu.addItem(.separator()) }
            for (title, command, key, modifiers) in group {
                let format = item(title, #selector(NoteWindowController.performEditorCommand(_:)), key, modifiers)
                format.representedObject = command.rawValue
                menu.addItem(format)
            }
        }
        return menu
    }

    private static func windowMenu() -> NSMenu {
        let menu = NSMenu(title: "Window")
        menu.addItem(item("Minimize", #selector(NSWindow.performMiniaturize(_:)), "m"))
        menu.addItem(item("Zoom", #selector(NSWindow.performZoom(_:))))
        menu.addItem(.separator())
        menu.addItem(item("Bring All to Front", #selector(NSApplication.arrangeInFront(_:))))
        return menu
    }

    private static func helpMenu() -> NSMenu {
        let menu = NSMenu(title: "Help")
        menu.addItem(item("Drawing Language", #selector(AppDelegate.showDrawingReference(_:))))
        return menu
    }

    // MARK: Helpers

    private static func item(
        _ title: String, _ action: Selector?, _ key: String = "",
        _ modifiers: NSEvent.ModifierFlags = .command
    ) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
        if !key.isEmpty { item.keyEquivalentModifierMask = modifiers }
        return item
    }

    private static func findItem(
        _ title: String, _ action: NSTextFinder.Action, _ key: String,
        _ modifiers: NSEvent.ModifierFlags = .command
    ) -> NSMenuItem {
        let item = self.item(title, #selector(NSResponder.performTextFinderAction(_:)), key, modifiers)
        item.tag = action.rawValue
        return item
    }

    private static func submenu(_ menu: NSMenu) -> NSMenuItem {
        let item = NSMenuItem(title: menu.title, action: nil, keyEquivalent: "")
        item.submenu = menu
        return item
    }
}
