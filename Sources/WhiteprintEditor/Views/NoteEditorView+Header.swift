import AppKit
import WhiteprintCore

// MARK: - Cover and icon

extension NoteEditorView {
    /// The note's page icon (an emoji); nil removes it. One undoable step.
    public func setIcon(_ icon: String?) {
        editFrontMatter(icon == nil ? "Remove Icon" : "Change Icon") { $0.icon = icon }
    }

    /// The note's cover, a `CoverGallery` id; nil removes it. One undoable step.
    public func setCover(_ cover: String?) {
        editFrontMatter(cover == nil ? "Remove Cover" : "Change Cover") { $0.cover = cover }
    }

    /// Changes the front matter like any other edit: undoable, and reported
    /// through `onChange` straight away so the document saves it.
    private func editFrontMatter(_ name: String, _ change: (inout FrontMatter) -> Void) {
        performBlockEdit(name) { document in
            change(&document.frontMatter)
            return nil
        }
        flushPendingChange()
    }

    func setUpPageHeader() {
        coverHeader.onEditIcon = { [weak self] rect in self?.showIconPicker(from: rect) }
        coverHeader.onAddCover = { [weak self] in
            self?.setCover(CoverGallery.random(excluding: nil).id)
        }
        coverHeader.onChangeCover = { [weak self] rect in self?.showCoverPicker(from: rect) }
        coverHeader.onRemoveCover = { [weak self] in self?.setCover(nil) }
    }

    /// Puts the header on the first page (or nowhere, when hidden) and shows
    /// the note's current cover and icon.
    func updatePageHeader() {
        let frontMatter = document.frontMatter
        coverHeader.cover = frontMatter.cover
        coverHeader.icon = frontMatter.icon
        for (index, page) in pageViews.enumerated() {
            page.header = index == 0 && showsCoverAndIcon ? coverHeader : nil
        }
    }

    private func showIconPicker(from rect: NSRect) {
        let picker = IconPicker(current: document.frontMatter.icon) { [weak self] icon in self?.setIcon(icon) }
        // Semi-transient, so it stays open while the system emoji picker is in use.
        show(picker, behavior: .semitransient, from: rect) { picker.popover = $0 }
    }

    private func showCoverPicker(from rect: NSRect) {
        let picker = CoverPicker(current: document.frontMatter.cover) { [weak self] cover in self?.setCover(cover) }
        show(picker, behavior: .transient, from: rect) { picker.popover = $0 }
    }

    private func show(_ controller: NSViewController, behavior: NSPopover.Behavior, from rect: NSRect,
                      attach: (NSPopover) -> Void) {
        guard window?.isVisible == true, coverHeader.superview != nil else { return }
        closeSlashMenu()
        let popover = NSPopover()
        popover.behavior = behavior
        popover.animates = !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        popover.contentViewController = controller
        attach(popover)
        popover.show(relativeTo: rect, of: coverHeader, preferredEdge: .maxY)
    }
}
