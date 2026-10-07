#include "EditorTestSupport.h"

#include "editor/MarkdownConcealment.h"
#include "render/Fonts.h"

#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QTextDocument>

using namespace wp;
using namespace wp::testing;
namespace S = wp::MarkdownStyler;

/// Layout modes, concealed Markdown, commands, the command toolbar and decks.
class EditorFeatureTests : public QObject
{
    Q_OBJECT

    EditorHarness h;
    NoteEditorView &editor() { return *h.editor; }
    const QString deckNote = "Intro\n\n```cards id=c1\n# Deck\nQ: q1\nA: a1\n```\n\nOutro";

    static QList<NoteBlock> L(std::initializer_list<NoteBlock> blocks) { return QList<NoteBlock>(blocks); }

    static QString markupAt(BlockTextView *view, int index)
    {
        const auto markup = S::markupOf(S::formatAt(*view->document(), index));
        return markup ? S::markupName(*markup) : QStringLiteral("<none>");
    }

    int realizedCount() const
    {
        int count = 0;
        for (PageView *p : h.editor->pageViews())
            count += p->isRealized ? 1 : 0;
        return count;
    }

private slots:
    void cleanup() { h.close(); }

    // MARK: Layout modes

    void a4UsesPaperSheetsAndScaledText()
    {
        h.open("Text");
        BlockTextView *view = editor().textViews().value(h.block(0, 0).id);
        QCOMPARE(view->fontSize(), 15.0);
        editor().setLayoutMode(PageLayoutMode::a4);
        const PageGeometry geometry = editor().geometry();
        QCOMPARE(view->width(), int(geometry.textWidth()));
        QCOMPARE(view->fontSize(), geometry.fontSize);
        QVERIFY(std::abs(fonts::sizeOf(S::formatAt(*view->document(), 0).font()) - geometry.fontSize) < 0.5);
        QVERIFY(std::abs(editor().pageViews()[0]->sheetRect().height() - geometry.minPageHeight()) <= 1);
        editor().setLayoutMode(PageLayoutMode::slides);
        QCOMPARE(view->fontSize(), 15.0);
        QCOMPARE(double(view->width()), PageGeometry::maxTextWidth);
    }

    void a4MarksPrintedPageBreaks()
    {
        QStringList lines;
        for (int i = 1; i <= 80; ++i)
            lines << QString("Line %1 of a long page.").arg(i);
        h.open(lines.join("\n\n"));
        QVERIFY(editor().pageViews()[0]->pageBreaks().empty());
        editor().setLayoutMode(PageLayoutMode::a4);
        QVERIFY(editor().pageViews()[0]->pageBreaks().size() >= 1);
        QVERIFY(editor().pageViews()[0]->sheetRect().height() > editor().geometry().minPageHeight());
    }

    void switchingModesKeepsTheTopBlock()
    {
        QStringList pages;
        for (int i = 1; i <= 30; ++i)
            pages << QString("# Page %1\n").arg(i) + QString("Some text that fills the page.\n\n").repeated(12);
        h.open(pages.join("+++page\n"));
        editor().scrollToPage(12);
        editor().scrollToY(editor().scrollTop() + 150);
        const auto top = editor().topBlock();
        QVERIFY(top);
        editor().setLayoutMode(PageLayoutMode::a4);
        QVERIFY(editor().topBlock() && editor().topBlock()->first == top->first);
        editor().setLayoutMode(PageLayoutMode::slides);
        QVERIFY(editor().topBlock() && editor().topBlock()->first == top->first);
        QVERIFY(std::abs(editor().topBlock()->second - top->second) < 0.05);
    }

    // MARK: Page themes

    void changingThePaletteRestylesInPlace()
    {
        h.open("Intro text\n+++page\nSecond page");
        BlockTextView *view = editor().textViews().value(h.block(0, 0).id);
        QVERIFY(!editor().blueprintPalette().showsGrid); // pages default to the Paper theme
        QCOMPARE(editor().pageViews().size(), 2);
        QVERIFY(!editor().pageViews()[0]->followsAnotherPage());
        QVERIFY(editor().pageViews()[1]->followsAnotherPage());
        editor().setBlueprintPalette(BlueprintPalette::blueprint());
        QCOMPARE(S::formatAt(*view->document(), 0).foreground().color(), QColor(Qt::white));
        QVERIFY(view->blueprintPalette() == BlueprintPalette::blueprint());
        QVERIFY(editor().pageViews()[0]->blueprintPalette() == BlueprintPalette::blueprint());
        editor().setBlueprintPalette(BlueprintPalette::sepia());
        QCOMPARE(S::formatAt(*view->document(), 0).foreground().color(), BlueprintPalette::sepia().text);
        QCOMPARE(editor().scrollArea()->viewport()->palette().color(QPalette::Window), BlueprintPalette::sepia().canvas);
    }

    // MARK: Concealed Markdown

    void hidingSyntaxConcealsAllButTheCaretParagraph()
    {
        h.open("# Title\n**bold** and `code`\n- [ ] task");
        BlockTextView *view = h.focusText(0, 0, 3);
        editor().setShowsMarkdownSyntax(false);
        QCOMPARE(markupAt(view, 0), QString("<none>")); // the caret's line shows its markup
        QCOMPARE(markupAt(view, 8), QString("hidden"));
        QCOMPARE(markupAt(view, 28), QString("hidden")); // the task's dash
        QCOMPARE(markupAt(view, 30), QString("checkbox"));

        view->setSelectedRange(TextRange(10, 0));
        QCOMPARE(markupAt(view, 0), QString("hidden"));
        QCOMPARE(markupAt(view, 8), QString("<none>"));
        QVERIFY(view->revealedRange() == TextRange(8, 20));

        view->clearFocus();
        QVERIFY(!view->revealedRange());
        QCOMPARE(markupAt(view, 8), QString("hidden"));
        editor().setShowsMarkdownSyntax(true);
        QCOMPARE(markupAt(view, 0), QString("<none>"));
        QCOMPARE(view->string(), QString("# Title\n**bold** and `code`\n- [ ] task")); // the text stays raw Markdown
    }

    void concealedGlyphsTakeNoSpace()
    {
        h.open("Intro\n# Title\n- item");
        editor().setShowsMarkdownSyntax(false);
        BlockTextView *view = editor().textViews().value(h.block(0, 0).id);
        view->sizeToFit();
        const QTextDocument *doc = view->document();
        // The "# " before the heading takes no space, so the heading starts at the margin.
        QVERIFY(ConcealedGlyphs::characterRect(doc, 6).width() < 1.0);
        QVERIFY(std::abs(ConcealedGlyphs::characterRect(doc, 8).left()) < 1.0);
        // The bullet keeps its place at the margin.
        QVERIFY(std::abs(ConcealedGlyphs::characterRect(doc, 14).left()) < 1.0);
        QCOMPARE(ConcealedGlyphs::bulletCharacter(), QChar(0x2022));
    }

    void typingAndCopyingWhileConcealed()
    {
        h.open("**a** b\n**c**");
        editor().setShowsMarkdownSyntax(false);
        BlockTextView *view = h.focusText(0, 0, 7);
        QTest::keyClicks(view, "!");
        QVERIFY(h.firstPageBlocks() == L({T("**a** b!\n**c**")}));
        view->setSelectedRange(TextRange(0, 14));
        view->copy();
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("**a** b!\n**c**"));
    }

    void concealedCheckboxStillToggles()
    {
        h.open("Intro\n- [ ] task");
        editor().setShowsMarkdownSyntax(false);
        BlockTextView *view = editor().textViews().value(h.block(0, 0).id);
        view->sizeToFit();
        const QRectF box = view->rect(TextRange(8, 3));
        QVERIFY(view->checkbox(box.center()) == TextRange(8, 3));
        editor().blockTextViewToggleCheckbox(view, 8);
        QVERIFY(h.firstPageBlocks() == L({T("Intro\n- [x] task")}));
    }

    // MARK: Commands

    void performFormatsTheSelectionUndoably()
    {
        h.open("Title\nword");
        h.focusText(0, 0, 6, 4);
        editor().perform(EditorCommand::bold);
        QVERIFY(h.firstPageBlocks() == L({T("Title\n**word**")}));
        QCOMPARE(editor().undoActionName(), QString("Bold"));
        editor().undo();
        QVERIFY(h.firstPageBlocks() == L({T("Title\nword")}));
        h.focusText(0, 0, 2);
        editor().perform(EditorCommand::heading1);
        QVERIFY(h.firstPageBlocks() == L({T("# Title\nword")}));
    }

    void performInsertCommands()
    {
        h.open("One");
        h.focusText(0, 0, 3);
        editor().perform(EditorCommand::newPage);
        QCOMPARE(editor().note().pages().size(), 2);
        editor().perform(EditorCommand::flashcards);
        QCOMPARE(editor().note().decks().size(), 1);
        QCOMPARE(editor().note().decks()[0].id, QString("c1"));
        QVERIFY(editor().deckEditor());
        editor().deckEditor()->commit();
        editor().perform(EditorCommand::drawing);
        QCOMPARE(editor().note().drawings().size(), 1);
        QCOMPARE(editor().note().drawings()[0].id, QString("d1"));
        editor().drawingEditor()->commit();
    }

    void textCommandsNeedATextBlock()
    {
        h.open(deckNote);
        editor().focus(NoteEditorView::Focus::block(h.block(0, 1).id));
        editor().perform(EditorCommand::bold);
        QVERIFY(editor().note() == Note::parsing(deckNote));
    }

    // MARK: Command toolbar (stands in for the Touch Bar)

    void toolbarActionsRunCommands()
    {
        h.open("word");
        h.focusText(0, 0, 0, 4);
        EditorToolbar *toolbar = editor().commandActions();
        QCOMPARE(EditorToolbar::defaultCommands().size(), 12);
        QCOMPARE(toolbar->actions().size(), 12);
        QCOMPARE(EditorToolbar::buttonCommands().size(), 8);
        QCOMPARE(EditorToolbar::styleCommands().size(), 4);

        QAction *bold = toolbar->action(EditorCommand::bold);
        QCOMPARE(bold->toolTip(), QString("Bold"));
        bold->trigger();
        QVERIFY(h.firstPageBlocks() == L({T("**word**")}));

        QAction *h2 = toolbar->action(EditorCommand::heading2);
        QCOMPARE(h2->text(), QString("H2"));
        h2->trigger();
        QVERIFY(h.firstPageBlocks() == L({T("## **word**")}));

        for (EditorCommand command : EditorToolbar::buttonCommands())
            QVERIFY(toolbar->action(command));
        QVERIFY(toolbar->makeToolBar());
    }

    // MARK: Decks

    void deckBlockShowsTheDeck()
    {
        h.open(deckNote);
        DeckBlockView *deckView = editor().deckViews().value(h.block(0, 1).id);
        QVERIFY(deckView);
        QCOMPARE(deckView->deck().title.value_or(QString()), QString("Deck"));
        const int closed = deckView->height();
        deckView->toggleAnswer(0);
        editor().layoutSubtreeIfNeeded();
        QVERIFY(deckView->height() > closed);
        QVERIFY(deckView->revealed() == QSet<int>{0});
    }

    void studyButtonCallsBack()
    {
        h.open(deckNote);
        QList<CardDeck> studied;
        editor().onStudyDeck = [&](const CardDeck &d) { studied.append(d); };
        editor().deckBlockViewRequestsStudy(editor().deckViews().value(h.block(0, 1).id));
        QCOMPARE(studied.size(), 1);
        QCOMPARE(studied[0].id, QString("c1"));
    }

    void editingADeckIsUndoable()
    {
        h.open(deckNote);
        const BlockID id = h.block(0, 1).id;
        editor().editDeck(id);
        DeckEditor *deckEditor = editor().deckEditor();
        QVERIFY(deckEditor);
        deckEditor->addCard();
        QCOMPARE(deckEditor->draft().cards.size(), 2);
        const CardDeck edited(QString(), QString("New"), {Flashcard("q2", "a2", QString("r"))});
        editor().commitDeck(id, edited);
        QVERIFY(editor().note().decks() == QList<CardDeck>{CardDeck("c1", QString("New"), {Flashcard("q2", "a2", QString("r"))})});
        QCOMPARE(editor().deckViews().value(id)->deck().title.value_or(QString()), QString("New"));
        editor().undo();
        QCOMPARE(editor().note().decks().first().title.value_or(QString()), QString("Deck"));
    }

    void insertDeckSplitsTheTextAndIDsAreNeverReused()
    {
        h.open("Before after");
        h.focusText(0, 0, 7);
        editor().insertDeckAtSelection();
        QVERIFY(h.firstPageBlocks() == L({T("Before "), NoteBlock::cardsBlock(CardDeck("c1", std::nullopt, {})), T("after")}));
        editor().closeDeckEditor(true);
        editor().deleteBlock(h.block(0, 1).id);
        QCOMPARE(editor().note().frontMatter.value("last-cards").value_or(QString()), QString("1"));
        editor().undo();
        editor().undo();
        QCOMPARE(editor().note().frontMatter.value("last-cards").value_or(QString()), QString("1")); // undo doesn't give the id back
        h.focusText(0, 0, 0);
        editor().insertDeckAtSelection();
        QCOMPARE(editor().note().decks().size(), 1);
        QCOMPARE(editor().note().decks()[0].id, QString("c2"));
    }

    void slashFlashcards()
    {
        h.open("");
        BlockTextView *view = h.focusText(0, 0, 0);
        QTest::keyClicks(view, "/flash");
        QVERIFY(editor().slashMenu());
        QVERIFY(editor().slashMenu()->state.selected() == SlashCommand::flashcards);
        editor().chooseSlashCommand(std::nullopt);
        QCOMPARE(editor().note().decks().size(), 1);
        QVERIFY(editor().deckEditor());
    }

    void arrowsBackspaceAndDeleteOnADeck()
    {
        h.open(deckNote);
        h.focusText(0, 0, 5);
        h.press(Qt::Key_Down);
        QVERIFY(qobject_cast<DeckBlockView *>(h.focusWidget()));
        h.press(Qt::Key_Down);
        QCOMPARE(h.focusedText()->string(), QString("Outro"));
        editor().focus(NoteEditorView::Focus::text(h.block(0, 2).id, TextRange(0, 0)));
        h.press(Qt::Key_Backspace);
        QVERIFY(qobject_cast<DeckBlockView *>(h.focusWidget()));
        h.press(Qt::Key_Backspace);
        QVERIFY(h.firstPageBlocks() == L({T("Intro"), T("Outro")}));
        editor().undo();
        QCOMPARE(editor().note().decks().size(), 1);
        QCOMPARE(editor().note().decks()[0].id, QString("c1"));
    }

    void duplicateAndMoveADeck()
    {
        h.open(deckNote);
        const BlockID id = h.block(0, 1).id;
        editor().duplicateBlock(id);
        QCOMPARE(editor().note().decks().size(), 2);
        QCOMPARE(editor().note().decks()[1].id, QString("c2"));
        editor().moveBlock(id, -1);
        const NoteBlock first = h.firstPageBlocks()[0];
        QVERIFY2(first.kind == BlockKind::cards, "deck should move up");
        QCOMPARE(first.deck.id, QString("c1"));
    }

    void setNoteKeepsTheDeckView()
    {
        h.open(deckNote);
        DeckBlockView *deckView = editor().deckViews().value(h.block(0, 1).id);
        const Note changed = Note::parsing("Intro!\n\n```cards id=c1\nQ: new\nA: card\n```\n\nOutro");
        editor().setNote(changed, true);
        QVERIFY(editor().deckViews().value(h.block(0, 1).id) == deckView);
        QCOMPARE(deckView->deck().cards.first().question, QString("new"));
    }

    void deckEditorClosesWhenItsDeckDisappears()
    {
        h.open(deckNote);
        editor().editDeck(h.block(0, 1).id);
        QVERIFY(editor().deckEditor());
        editor().setNote(Note::parsing("Intro"), true);
        QVERIFY(!editor().deckEditor());
        QCOMPARE(editor().note().decks().size(), 0);
    }
};

QTEST_MAIN(EditorFeatureTests)
#include "EditorFeatureTests.moc"
