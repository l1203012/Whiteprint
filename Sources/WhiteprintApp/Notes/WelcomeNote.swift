import WhiteprintCore

/// The short tour created when the notes folder is empty on launch.
enum WelcomeNote {
    static let title = "Welcome"

    static let source = """
    ---
    whiteprint: 1
    title: Welcome
    ---
    # Welcome to Whiteprint

    Notes on blueprint paper. Write in plain Markdown, or type `/` for headings, lists, checklists, code and drawings.

    ## Get started

    - [x] Open Whiteprint
    - [ ] Press ⌘K to jump to a note or run a command
    - [ ] Type `/` and pick *Drawing*, then double-click it to edit
    - [ ] Connect Claude in Settings (⌘,)

    ## How it fits together

    ```wp id=d1
    flow You>Whiteprint>Claude
    text "Claude writes and draws in your notes, on your own subscription"
    ```

    +++page

    # Study plans

    Drop lecture slides, PDFs or Word files into the study panel (File ▸ Import for Study Plan…) and choose **Generate study plan**. Text is extracted on your Mac; only that text is shared, and only with your own Claude client.
    """

    static var note: Note {
        // The source is fixed and valid; parsing can't fail.
        (try? Note(parsing: source)) ?? Note(title: title)
    }
}
