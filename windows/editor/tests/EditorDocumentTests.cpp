#include "editor/EditorDocument.h"

#include <QtTest>

using namespace wp;

class EditorDocumentTests : public QObject
{
    Q_OBJECT

    static EditorDocument document(const QString &text) { return EditorDocument(Note::parsing(text)); }

    static QList<QStringList> contents(const EditorDocument &doc)
    {
        QList<QStringList> result;
        for (const EditorPage &page : doc.pages()) {
            QStringList blocks;
            for (const EditorBlock &block : page.blocks) {
                switch (block.kind) {
                case BlockKind::text: blocks << "t:" + block.textValue; break;
                case BlockKind::drawing: blocks << "d:" + block.drawingValue.id + ":" + block.drawingValue.source; break;
                case BlockKind::cards: blocks << QString("c:%1:%2").arg(block.deckValue.id).arg(block.deckValue.cards.size()); break;
                }
            }
            result.append(blocks);
        }
        return result;
    }

    static QList<QStringList> L(std::initializer_list<QStringList> l) { return QList<QStringList>(l); }

private slots:
    void everyPageEndsWithATextBlock()
    {
        const auto doc = document("A\n\n```wp id=d1\nbox a\n```\n+++page\n+++page\n```wp id=d2\nbox b\n```");
        QCOMPARE(contents(doc), L({{"t:A", "d:d1:box a", "t:"}, {"t:"}, {"d:d2:box b", "t:"}}));
    }

    void noteDropsEmptyTextAndRoundTrips()
    {
        const Note note = Note::parsing("# Title\n\n```wp id=d1\nbox a\n```\n+++page\nTwo");
        const EditorDocument doc(note);
        QVERIFY(doc.note() == note);
        QCOMPARE(doc.note().serialized(), note.serialized());
    }

    void noteTrimsSurroundingBlankLines()
    {
        auto doc = document("A");
        const BlockID id = doc.pages()[0].blocks[0].id;
        doc.setText("\nA\nB\n\n", id);
        QVERIFY(doc.note().pages()[0].blocks == QList<NoteBlock>{NoteBlock::textBlock("A\nB")});
    }

    void insertDrawingSplitsTextAtOffset()
    {
        auto doc = document("first line\nsecond line");
        const BlockID text = doc.pages()[0].blocks[0].id;
        const auto drawing = doc.insertDrawing("box a", text, 11);
        QVERIFY(drawing);
        QCOMPARE(contents(doc), L({{"t:first line", "d:d1:box a", "t:second line"}}));
        QVERIFY(doc.pages()[0].blocks[0].id == text);
        QVERIFY(doc.pages()[0].blocks[1].id == *drawing);
        QCOMPARE(doc.note().serialized(),
                 QString("---\nwhiteprint: 1\nlast-drawing: 1\n---\n\nfirst line\n\n```wp id=d1\nbox a\n```\n\nsecond line\n"));
    }

    void insertDrawingAtStartPutsItBeforeTheText()
    {
        auto doc = document("text");
        const BlockID text = doc.pages()[0].blocks[0].id;
        doc.insertDrawing("", text, 0);
        QCOMPARE(contents(doc), L({{"d:d1:", "t:text"}}));
        QVERIFY(doc.pages()[0].blocks[1].id == text);
    }

    void drawingIDsAreNeverReused()
    {
        auto doc = document("```wp id=d1\nbox a\n```");
        const BlockID first = doc.pages()[0].blocks[0].id;
        doc.removeBlock(first);
        doc.appendDrawing("box b", 0);
        QCOMPARE(doc.note().drawings().size(), 1);
        QCOMPARE(doc.note().drawings()[0].id, QString("d2"));
    }

    void appendDrawingGoesBeforeTrailingEmptyText()
    {
        auto doc = document("A\n+++page\nB");
        doc.appendDrawing("box a", 1);
        QCOMPARE(contents(doc), L({{"t:A"}, {"t:B", "d:d1:box a", "t:"}}));
    }

    void insertAndRemovePages()
    {
        auto doc = document("A\n+++page\nB");
        const BlockID added = doc.insertPage(0);
        QVERIFY(doc.location(added) == (BlockLocation{1, 0}));
        QCOMPARE(contents(doc), L({{"t:A"}, {"t:"}, {"t:B"}}));
        doc.removePage(1);
        QCOMPARE(contents(doc), L({{"t:A"}, {"t:B"}}));
        doc.removePage(0);
        doc.removePage(0);
        QCOMPARE(contents(doc), L({{"t:B"}}));
    }

    void mergePageWithPrevious()
    {
        auto doc = document("A\n\n```wp id=d1\nx\n```\n+++page\nB");
        const BlockID b = doc.pages()[1].blocks[0].id;
        doc.mergePageWithPrevious(1);
        QCOMPARE(contents(doc), L({{"t:A", "d:d1:x", "t:B"}}));
        QVERIFY(doc.pages()[0].blocks[2].id == b);
    }

    void removeBlockReturnsNeighbourAndCollapsesText()
    {
        auto doc = document("A\n\n```wp id=d1\nx\n```");
        const BlockID a = doc.pages()[0].blocks[0].id;
        const BlockID drawing = doc.pages()[0].blocks[1].id;
        QVERIFY(doc.removeBlock(drawing) == a);
        QCOMPARE(contents(doc), L({{"t:A"}}));
    }

    void duplicateDrawingGetsFreshID()
    {
        auto doc = document("```wp id=d4\nbox a\n```");
        const auto copy = doc.duplicateBlock(doc.pages()[0].blocks[0].id);
        QVERIFY(copy);
        QVERIFY(doc.block(*copy)->drawing() == Drawing("d5", "box a"));
        QCOMPARE(contents(doc), L({{"d:d4:box a", "d:d5:box a", "t:"}}));
    }

    void moveBlockWithinAndAcrossPages()
    {
        auto doc = document("A\n\n```wp id=d1\nx\n```\n\nB\n+++page\nC");
        const BlockID drawing = doc.pages()[0].blocks[1].id;
        QVERIFY(doc.moveBlock(drawing, -1));
        QCOMPARE(contents(doc), L({{"d:d1:x", "t:A", "t:B"}, {"t:C"}}));
        QVERIFY(!doc.moveBlock(drawing, -1));
        const BlockID b = doc.pages()[0].blocks[2].id;
        QVERIFY(doc.moveBlock(b, 1));
        QCOMPARE(contents(doc), L({{"d:d1:x", "t:A"}, {"t:B", "t:C"}}));
        QVERIFY(doc.moveBlock(b, -1));
        QCOMPARE(contents(doc), L({{"d:d1:x", "t:A", "t:B"}, {"t:C"}}));
    }

    void trailingEmptyTextIsNotASwapPartner()
    {
        auto doc = document("```wp id=d1\nx\n```");
        const BlockID drawing = doc.pages()[0].blocks[0].id;
        QVERIFY(!doc.moveBlock(drawing, 1));
        QCOMPARE(contents(doc), L({{"d:d1:x", "t:"}}));
    }

    void mergeWithPreviousNeedsTextBefore()
    {
        auto doc = document("```wp id=d1\nx\n```\n\nA");
        QVERIFY(!doc.mergeWithPrevious(doc.pages()[0].blocks[1].id));
        QVERIFY(!doc.mergeWithPrevious(doc.pages()[0].blocks[0].id));
    }

    void mergeTwoTextBlocks()
    {
        auto doc = document("A\n\n```wp id=d1\nx\n```\n\nB");
        const BlockID a = doc.pages()[0].blocks[0].id;
        const BlockID b = doc.pages()[0].blocks[2].id;
        doc.removeBlock(doc.pages()[0].blocks[1].id);
        QCOMPARE(contents(doc), L({{"t:A", "t:B"}}));
        const auto merged = doc.mergeWithPrevious(b);
        QVERIFY(merged);
        QVERIFY(merged->block == a);
        QCOMPARE(merged->offset, 2);
        QCOMPARE(contents(doc), L({{"t:A\nB"}}));
    }

    void textBlockAfterDrawing()
    {
        auto doc = document("```wp id=d1\nx\n```\n\n```wp id=d2\ny\n```");
        const BlockID first = doc.pages()[0].blocks[0].id;
        const auto inserted = doc.textBlockAfter(first);
        QVERIFY(inserted);
        QVERIFY(doc.location(*inserted) == (BlockLocation{0, 1}));
        const BlockID last = doc.pages()[0].blocks[2].id;
        QVERIFY(doc.textBlockAfter(last) == doc.pages()[0].blocks[3].id);
    }

    void neighboursCrossPages()
    {
        const auto doc = document("A\n+++page\n```wp id=d1\nx\n```");
        const BlockID a = doc.pages()[0].blocks[0].id;
        QCOMPARE(doc.blockAfter(a)->drawingValue.id, QString("d1"));
        QVERIFY(doc.blockBefore(doc.pages()[1].blocks[0].id)->id == a);
        QVERIFY(!doc.blockBefore(a));
    }

    // MARK: Reconciliation

    void reconcileKeepsIDsOfMatchingBlocks()
    {
        const auto doc = document("A\n\n```wp id=d1\nx\n```\n\nB\n+++page\nC");
        QList<QList<BlockID>> ids;
        QList<PageID> pageIDs;
        for (const EditorPage &page : doc.pages()) {
            QList<BlockID> row;
            for (const EditorBlock &b : page.blocks)
                row << b.id;
            ids << row;
            pageIDs << page.id;
        }
        const Note edited = Note::parsing("A2\n\n```wp id=d0\nnew\n```\n\n```wp id=d1\nx2\n```\n\nB\n+++page\nC\n+++page\nD");
        const auto result = doc.reconciled(edited);
        QCOMPARE(result.pages().size(), 3);
        QVERIFY(result.pages()[0].blocks[0].id == ids[0][0]); // first text block by position
        QVERIFY(!(result.pages()[0].blocks[1].id == ids[0][1])); // new drawing is new
        QVERIFY(result.pages()[0].blocks[2].id == ids[0][1]); // drawing matched by id
        QVERIFY(result.pages()[0].blocks[3].id == ids[0][2]); // second text block by position
        QVERIFY(result.pages()[1].blocks[0].id == ids[1][0]);
        QVERIFY(result.pages()[0].id == pageIDs[0] && result.pages()[1].id == pageIDs[1]);
        QCOMPARE(result.pages()[0].blocks[0].textValue, QString("A2"));
        QVERIFY(result.note().pages()[0].blocks[2] == NoteBlock::drawingBlock(Drawing("d1", "x2")));
    }

    void reconcileKeepsEditorTextWhenOnlyBlankLinesDiffer()
    {
        auto doc = document("A");
        const BlockID id = doc.pages()[0].blocks[0].id;
        doc.setText("A\n", id);
        const auto result = doc.reconciled(doc.note());
        QCOMPARE(result.pages()[0].blocks[0].textValue, QString("A\n"));
        QVERIFY(result.pages()[0].blocks[0].id == id);
    }

    void reconcileNewIDsDontCollide()
    {
        const auto doc = document("A");
        const auto result = doc.reconciled(Note::parsing("X\n+++page\nY\n+++page\nZ"));
        QSet<int> all;
        int count = 0;
        for (const EditorPage &page : result.pages()) {
            for (const EditorBlock &b : page.blocks) {
                all.insert(b.id.rawValue);
                ++count;
            }
        }
        QCOMPARE(all.size(), count);
    }

    void restoringKeepsDrawingHighWaterMark()
    {
        auto doc = document("A");
        const auto before = doc;
        doc.appendDrawing("x", 0);
        auto restored = doc.restoring(before);
        QCOMPARE(restored.note().drawings().size(), 0);
        restored.appendDrawing("y", 0);
        QCOMPARE(restored.note().drawings()[0].id, QString("d2"));
    }
};

QTEST_APPLESS_MAIN(EditorDocumentTests)
#include "EditorDocumentTests.moc"
