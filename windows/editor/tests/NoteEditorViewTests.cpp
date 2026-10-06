#include "EditorTestSupport.h"

using namespace wp;
using namespace wp::testing;

class NoteEditorViewTests : public QObject
{
    Q_OBJECT

    EditorHarness h;
    NoteEditorView &editor() { return *h.editor; }

    static QList<NoteBlock> L(std::initializer_list<NoteBlock> blocks) { return QList<NoteBlock>(blocks); }

private slots:
    void cleanup() { h.close(); }

    // MARK: Typing and Markdown behaviours

    void typingUpdatesTheNote()
    {
        h.open("Hello");
        h.focusText(0, 0);
        h.type(" world");
        QVERIFY(h.firstPageBlocks() == L({T("Hello world")}));
    }

    void returnContinuesAndEndsAChecklist()
    {
        h.open("- [x] milk");
        h.focusText(0, 0);
        h.press(Qt::Key_Return);
        h.type("eggs");
        QVERIFY(h.firstPageBlocks() == L({T("- [x] milk\n- [ ] eggs")}));
        h.press(Qt::Key_Return);
        h.press(Qt::Key_Return);
        h.type("done");
        QVERIFY(h.firstPageBlocks() == L({T("- [x] milk\n- [ ] eggs\ndone")}));
    }

    void tabIndentsListItems()
    {
        h.open("- a\n- b");
        h.focusText(0, 0);
        h.press(Qt::Key_Tab);
        QVERIFY(h.firstPageBlocks() == L({T("- a\n    - b")}));
        h.press(Qt::Key_Backtab, Qt::ShiftModifier);
        QVERIFY(h.firstPageBlocks() == L({T("- a\n- b")}));
    }

    void bracketShortcutMakesAChecklist()
    {
        h.open("");
        h.focusText(0, 0);
        h.type("[] buy");
        QVERIFY(h.firstPageBlocks() == L({T("- [ ] buy")}));
    }

    void clickingACheckboxTogglesItUndoably()
    {
        h.open("- [ ] task");
        BlockTextView *view = editor().textViews().value(h.block(0, 0).id);
        QVERIFY(view);
        editor().blockTextViewToggleCheckbox(view, 3);
        QVERIFY(h.firstPageBlocks() == L({T("- [x] task")}));
        editor().undo();
        QVERIFY(h.firstPageBlocks() == L({T("- [ ] task")}));
    }

    void checkboxHitTesting()
    {
        h.open("- [ ] task");
        BlockTextView *view = editor().textViews().value(h.block(0, 0).id);
        QVERIFY(view);
        const QRectF box = view->rect(TextRange(2, 3));
        QVERIFY(view->checkbox(box.center()) == TextRange(2, 3));
        const QRectF word = view->rect(TextRange(7, 2));
        QVERIFY(!view->checkbox(word.center()));
    }

    // MARK: Slash menu

    void slashMenuTurnsALineIntoAHeading()
    {
        h.open("Title");
        h.focusText(0, 0);
        h.type(" /h2");
        QVERIFY(editor().slashMenu());
        QVERIFY(editor().slashMenu()->state.selected() == SlashCommand::heading2);
        QVERIFY(!editor().slashMenuView()->isHidden());
        h.press(Qt::Key_Return);
        QVERIFY(!editor().slashMenu());
        QVERIFY(editor().slashMenuView()->isHidden());
        QVERIFY(h.firstPageBlocks() == L({T("## Title ")}));
    }

    void slashMenuArrowsAndEscape()
    {
        h.open("");
        h.focusText(0, 0);
        h.type("/");
        QVERIFY(editor().slashMenu());
        QVERIFY(editor().slashMenu()->state.selected() == SlashCommand::heading1);
        h.press(Qt::Key_Down);
        h.press(Qt::Key_Down);
        QVERIFY(editor().slashMenu()->state.selected() == SlashCommand::heading3);
        h.press(Qt::Key_Up);
        QVERIFY(editor().slashMenu()->state.selected() == SlashCommand::heading2);
        h.press(Qt::Key_Escape);
        QVERIFY(!editor().slashMenu());
        QVERIFY(h.firstPageBlocks() == L({T("/")}));
    }

    void slashDoesNotOpenInsideAWord()
    {
        h.open("and");
        h.focusText(0, 0);
        h.type("/or");
        QVERIFY(!editor().slashMenu());
    }

    void slashDrawingSplitsTheBlockAndOpensTheEditor()
    {
        h.open("First\nSecond");
        h.focusText(0, 0, 6);
        h.type("/draw");
        h.press(Qt::Key_Return);
        QVERIFY(h.firstPageBlocks() == L({T("First"), D("d1", ""), T("Second")}));
        QVERIFY(editor().drawingEditor());
        const BlockID id = editor().drawingEditor()->blockID();
        QCOMPARE(editor().document().block(id)->drawingValue.id, QString("d1"));
        editor().drawingEditor()->commit();
    }

    void slashNewPageAddsAPageAfterThisOne()
    {
        h.open("One\n+++page\nTwo");
        h.focusText(0, 0);
        h.type(" /new page");
        h.press(Qt::Key_Return);
        const auto &pages = editor().note().pages();
        QCOMPARE(pages.size(), 3);
        QVERIFY(pages[0].blocks == L({T("One ")}));
        QVERIFY(pages[1].blocks.isEmpty());
        QVERIFY(pages[2].blocks == L({T("Two")}));
        BlockTextView *focused = h.focusedText();
        QVERIFY(focused);
        QVERIFY(editor().document().location(focused->blockID()) == (BlockLocation{1, 0}));
    }

    // MARK: Moving between blocks

    void arrowKeysMoveBetweenBlocksAndPages()
    {
        h.open("A\n\n```wp id=d1\nbox a\n```\n\nB\n+++page\nC");
        h.focusText(0, 0);
        h.press(Qt::Key_Down);
        QVERIFY(qobject_cast<DrawingBlockView *>(h.focusWidget()));
        h.press(Qt::Key_Down);
        QCOMPARE(h.focusedText()->string(), QString("B"));
        h.press(Qt::Key_Down);
        QCOMPARE(h.focusedText()->string(), QString("C"));
        h.press(Qt::Key_Up);
        h.press(Qt::Key_Up);
        QVERIFY(qobject_cast<DrawingBlockView *>(h.focusWidget()));
        h.press(Qt::Key_Up);
        QCOMPARE(h.focusedText()->string(), QString("A"));
    }

    void arrowStaysInsideMultilineText()
    {
        h.open("one\ntwo\nthree");
        BlockTextView *view = h.focusText(0, 0, 5);
        h.press(Qt::Key_Down);
        QVERIFY(h.focusedText() == view);
        QCOMPARE(view->selectedRange().location, 9);
    }

    void backspaceSelectsThenDeletesADrawing()
    {
        h.open("A\n\n```wp id=d1\nbox a\n```\n\nB");
        h.focusText(0, 2, 0);
        h.press(Qt::Key_Backspace);
        QVERIFY(qobject_cast<DrawingBlockView *>(h.focusWidget()));
        h.press(Qt::Key_Backspace);
        QVERIFY(h.firstPageBlocks() == L({T("A"), T("B")}));
        QCOMPARE(h.focusedText()->string(), QString("A"));
        editor().undo();
        QVERIFY(h.firstPageBlocks() == L({T("A"), D("d1", "box a"), T("B")}));
        editor().redo();
        QVERIFY(h.firstPageBlocks() == L({T("A"), T("B")}));
    }

    void backspaceOnAnEmptyPageRemovesIt()
    {
        h.open("One\n+++page\n");
        QCOMPARE(editor().note().pages().size(), 2);
        h.focusText(1, 0);
        h.press(Qt::Key_Backspace);
        QCOMPARE(editor().note().pages().size(), 1);
        QVERIFY(h.focusedText()->selectedRange() == TextRange(3, 0));
    }

    void returnOnASelectedDrawingStartsTextAfterIt()
    {
        h.open("```wp id=d1\nx\n```\n\n```wp id=d2\ny\n```");
        editor().focus(NoteEditorView::Focus::block(h.block(0, 0).id));
        h.press(Qt::Key_Return);
        h.type("between");
        QVERIFY(h.firstPageBlocks() == L({D("d1", "x"), T("between"), D("d2", "y")}));
    }

    // MARK: Block menu operations

    void blockMenuOperationsAreUndoable()
    {
        h.open("A\n\n```wp id=d1\nx\n```");
        const BlockID drawing = h.block(0, 1).id;
        editor().moveBlock(drawing, -1);
        QVERIFY(h.firstPageBlocks() == L({D("d1", "x"), T("A")}));
        editor().undo();
        QVERIFY(h.firstPageBlocks() == L({T("A"), D("d1", "x")}));
        editor().duplicateBlock(drawing);
        QCOMPARE(editor().note().drawings().size(), 2);
        QCOMPARE(editor().note().drawings()[1].id, QString("d2"));
        editor().deleteBlock(h.block(0, 0).id);
        QCOMPARE(editor().note().drawings().size(), 2);
        QCOMPARE(h.firstPageBlocks().size(), 2);
    }

    // MARK: Drawings

    void committingTheDrawingEditorUpdatesTheDrawing()
    {
        h.open("```wp id=d1\nbox a\n```");
        editor().editDrawing(h.block(0, 0).id);
        DrawingSourceEditor *sourceEditor = editor().drawingEditor();
        QVERIFY(sourceEditor);
        editor().commitDrawing(sourceEditor->blockID(), "box b");
        QVERIFY(h.firstPageBlocks() == L({D("d1", "box b")}));
        QCOMPARE(editor().drawingViews().value(h.block(0, 0).id)->source(), QString("box b"));
        editor().undo();
        QVERIFY(h.firstPageBlocks() == L({D("d1", "box a")}));
    }

    void insertDrawingAtSelection()
    {
        h.open("Before after");
        h.focusText(0, 0, 7);
        editor().insertDrawingAtSelection("box n");
        QVERIFY(h.firstPageBlocks() == L({T("Before "), D("d1", "box n"), T("after")}));
        QVERIFY(editor().drawingEditor());
        editor().focus(std::nullopt);
        h.focusWidget()->clearFocus();
        editor().insertDrawingAtSelection("box m");
        QCOMPARE(editor().note().drawings().size(), 2);
        QCOMPARE(editor().note().drawings()[1].id, QString("d2"));
        QVERIFY(h.firstPageBlocks().last() == D("d2", "box m"));
    }

    // MARK: API

    void addPageMovesTheCaretThere()
    {
        h.open("One");
        h.focusText(0, 0);
        editor().addPage();
        h.type("Two");
        const auto &pages = editor().note().pages();
        QCOMPARE(pages.size(), 2);
        QVERIFY(pages[0].blocks == L({T("One")}));
        QVERIFY(pages[1].blocks == L({T("Two")}));
    }

    void onChangeIsCoalesced()
    {
        h.open("");
        QList<Note> received;
        editor().onChange = [&](const Note &n) { received.append(n); };
        h.focusText(0, 0);
        h.type("abc");
        QCOMPARE(received.size(), 0);
        editor().flushPendingChange();
        QCOMPARE(received.size(), 1);
        QVERIFY(received[0].pages()[0].blocks == L({T("abc")}));
        editor().flushPendingChange();
        QCOMPARE(received.size(), 1);
    }

    void setNoteKeepsTheCaretAndBlockViews()
    {
        h.open("hello world\n\n```wp id=d1\nx\n```\n\ntail");
        BlockTextView *view = h.focusText(0, 0, 6);
        DrawingBlockView *drawingView = editor().drawingViews().value(h.block(0, 1).id);
        QVERIFY(drawingView);
        const Note changed = Note::parsing("say hello world\n\n```wp id=d1\ny\n```\n\ntail");
        editor().setNote(changed, true);
        QVERIFY(editor().note() == changed);
        QVERIFY(h.focusedText() == view);
        QCOMPARE(view->string(), QString("say hello world"));
        QVERIFY(view->selectedRange() == TextRange(10, 0));
        QVERIFY(editor().drawingViews().value(h.block(0, 1).id) == drawingView);
        QCOMPARE(drawingView->source(), QString("y"));
    }

    void setNoteWhenTheFocusedBlockDisappears()
    {
        h.open("A\n+++page\nB");
        h.focusText(1, 0, 1);
        editor().setNote(Note::parsing("A"), true);
        QVERIFY(h.focusedText());
        QCOMPARE(h.focusedText()->string(), QString("A"));
        editor().setNote(Note::parsing("Z"), false);
        QVERIFY(!h.focusedText());
    }

    void setNoteIsUndoable()
    {
        h.open("mine");
        editor().setNote(Note::parsing("theirs"), true);
        editor().undo();
        QVERIFY(h.firstPageBlocks() == L({T("mine")}));
    }

    void typingIsUndoableAndCoalesced()
    {
        h.open("");
        h.focusText(0, 0);
        h.type("abc");
        editor().undo();
        QVERIFY(h.firstPageBlocks().isEmpty());
        QCOMPARE(editor().undoActionName(), QString());
        editor().redo();
        QVERIFY(h.firstPageBlocks() == L({T("abc")}));
        QCOMPARE(editor().undoActionName(), QString("Typing"));
    }

    void longNotesOnlyCreateViewsNearTheViewport()
    {
        QStringList pages;
        for (int i = 1; i <= 50; ++i)
            pages << QString("# Page %1\n").arg(i) + QString("Some text that fills the page.\n\n").repeated(20);
        h.open(pages.join("+++page\n"));
        int realized = 0;
        for (PageView *p : editor().pageViews())
            realized += p->isRealized ? 1 : 0;
        QVERIFY(realized < 6);
        QVERIFY(editor().textViews().size() < 6);

        QList<int> visible;
        editor().onVisiblePageChange = [&](int p) { visible.append(p); };
        editor().scrollToPage(40);
        QVERIFY(editor().pageViews()[39]->isRealized);
        QCOMPARE(visible.last(), 40);
        realized = 0;
        for (PageView *p : editor().pageViews())
            realized += p->isRealized ? 1 : 0;
        QVERIFY(realized < 12);
        const double top = editor().scrollTop();
        QVERIFY(std::abs(editor().pageViews()[39]->y() + PageGeometry::shadowInset - PageGeometry::pageGap - top) <= 1);
    }

    void pagesFollowTheWindowWidth()
    {
        h.open("Text");
        const int wide = editor().textViews().value(h.block(0, 0).id)->width();
        QCOMPARE(double(wide), PageGeometry::maxTextWidth);
        editor().resize(500, 600);
        editor().layoutSubtreeIfNeeded();
        const int narrow = editor().textViews().value(h.block(0, 0).id)->width();
        QVERIFY(narrow < 420);
        QVERIFY(editor().documentView()->width() <= editor().scrollArea()->viewport()->width());
    }

    void emptyPageShowsThePlaceholder()
    {
        h.open("");
        QVERIFY(editor().textViews().value(h.block(0, 0).id)->isAlonePlaceholder());
    }
};

QTEST_MAIN(NoteEditorViewTests)
#include "NoteEditorViewTests.moc"
