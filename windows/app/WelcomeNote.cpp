#include "app/WelcomeNote.h"

namespace wp::WelcomeNote {

QString source()
{
    return QString::fromUtf8(
        "---\n"
        "whiteprint: 1\n"
        "title: Welcome\n"
        "---\n"
        "# Welcome to Whiteprint\n"
        "\n"
        "Notes on blueprint paper. Write in plain Markdown, or type `/` for headings, lists, checklists, code and drawings.\n"
        "\n"
        "## Get started\n"
        "\n"
        "- [x] Open Whiteprint\n"
        "- [ ] Press Ctrl+K to jump to a note or run a command\n"
        "- [ ] Type `/` and pick *Drawing*, then double-click it to edit\n"
        "- [ ] Connect Claude in Settings (Ctrl+,)\n"
        "\n"
        "## How it fits together\n"
        "\n"
        "```wp id=d1\n"
        "flow You>Whiteprint>Claude\n"
        "text \"Claude writes and draws in your notes, on your own subscription\"\n"
        "```\n"
        "\n"
        "+++page\n"
        "\n"
        "# Study plans\n"
        "\n"
        "Drop lecture slides, PDFs or Word files into the study panel (File > Import for Study Plan...) and choose **Generate study plan**. "
        "Text is extracted on your PC; only that text is shared, and only with your own Claude client.");
}

Note note()
{
    // The source is fixed and valid; parsing can't fail.
    try {
        return Note::parsing(source());
    } catch (...) {
        return Note(title);
    }
}

} // namespace wp::WelcomeNote
