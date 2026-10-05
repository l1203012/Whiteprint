#pragma once
// Shared helpers for the editor widget tests (header only).
#include "core/Note.h"
#include "editor/NoteEditorView.h"

#include <QScrollArea>
#include <QtTest>
#include <memory>

namespace wp::testing {

inline NoteBlock T(const QString &text)
{
    return NoteBlock::textBlock(text);
}

inline NoteBlock D(const QString &id, const QString &source)
{
    return NoteBlock::drawingBlock(Drawing(id, source));
}

/// Hosts a `NoteEditorView` in a visible (offscreen) window and drives it with key events.
class EditorHarness
{
public:
    std::unique_ptr<NoteEditorView> editor;

    void open(const QString &text, int height = 800, int width = 900)
    {
        editor = std::make_unique<NoteEditorView>(Note::parsing(text));
        editor->setPresentsEditors(false);
        editor->resize(width, height);
        editor->show();
        (void)QTest::qWaitForWindowExposed(editor.get());
        editor->activateWindow();
        (void)QTest::qWaitForWindowActive(editor.get());
        editor->layoutSubtreeIfNeeded();
    }

    void close() { editor.reset(); }

    EditorBlock block(int page, int index) const { return editor->document().pages()[page].blocks[index]; }

    QList<NoteBlock> firstPageBlocks() const { return editor->note().pages()[0].blocks; }

    QWidget *focusWidget() const { return editor->window()->focusWidget(); }
    BlockTextView *focusedText() const { return qobject_cast<BlockTextView *>(focusWidget()); }

    /// Focuses a text block with the caret at `caret` (default: the end).
    BlockTextView *focusText(int page, int index, int caret = -1, int length = 0)
    {
        const EditorBlock b = block(page, index);
        const int c = caret < 0 ? int(b.textValue.size()) : caret;
        editor->focus(NoteEditorView::Focus::text(b.id, TextRange(c, length)));
        return focusedText();
    }

    void type(const QString &text)
    {
        QWidget *w = focusWidget();
        QVERIFY2(w, "nothing has the focus");
        QTest::keyClicks(w, text);
    }

    void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QWidget *w = focusWidget();
        QVERIFY2(w, "nothing has the focus");
        QTest::keyClick(w, key, modifiers);
    }
};

} // namespace wp::testing
