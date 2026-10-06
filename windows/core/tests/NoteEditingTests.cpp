#include "TestSupport.h"
#include "core/Note.h"

using namespace wp;
using namespace wp::testing;

class NoteEditingTests : public QObject
{
    Q_OBJECT

private:
    static Note note(const char *text) { return Note::parsing(QString::fromUtf8(text)); }

    static NoteBlock drawing(const char *id, const char *source)
    {
        return NoteBlock::drawingBlock(Drawing(id, source));
    }

private slots:
    void pageSourceIncludesDrawingIDs()
    {
        const Note n = note("Hello\n\n```wp\nbox a\n```\n+++page\nTwo");
        QCOMPARE(n.pageSource(1), QString("Hello\n\n```wp id=d1\nbox a\n```"));
        QCOMPARE(n.pageSource(2), QString("Two"));
    }

    void pageNumbersAreChecked()
    {
        const Note n = note("x");
        const auto error = caught<NoteEditError>([&] { n.pageSource(0); });
        QVERIFY(error.has_value());
        QCOMPARE(error->description(), QString("page 0 doesn't exist (note has 1 page)"));
        QVERIFY(caught<NoteEditError>([&] { n.pageSource(2); }).has_value());
    }

    void appendAddsBlocksAndFreshDrawingIDs()
    {
        Note n = note("```wp id=d1\nbox a\n```");
        n.write("More text\n\n```wp id=d1\nbox b\n```", 1, WriteMode::append);
        WP_EQ(n.pages()[0].blocks,
              (QList<NoteBlock>{drawing("d1", "box a"), NoteBlock::textBlock("More text"), drawing("d2", "box b")}));
    }

    void replaceKeepsIDsOfDrawingsWrittenBack()
    {
        Note n = note("A\n\n```wp id=d1\nbox a\n```\n\n```wp id=d2\nbox b\n```\n+++page\n```wp id=d3\nbox c\n```");
        // d1 is edited and kept, d2 is dropped, d3 lives on another page so it can't be claimed.
        n.write("B\n\n```wp id=d1\nbox a2\n```\n\n```wp id=d3\nbox x\n```\n\n```wp\nbox y\n```", 1, WriteMode::replace);
        WP_EQ(n.pages()[0].blocks,
              (QList<NoteBlock>{NoteBlock::textBlock("B"), drawing("d1", "box a2"), drawing("d4", "box x"),
                                drawing("d5", "box y")}));
        WP_EQ(n.pages()[1].blocks, (QList<NoteBlock>{drawing("d3", "box c")}));
    }

    void writingPageSeparatorsInsertsPages()
    {
        Note n = note("one\n+++page\nlast");
        n.write("two\n+++page\nthree", 1, WriteMode::append);
        QList<QList<NoteBlock>> blocks;
        for (const auto &p : n.pages())
            blocks.append(p.blocks);
        WP_EQ(blocks,
              (QList<QList<NoteBlock>>{{NoteBlock::textBlock("one"), NoteBlock::textBlock("two")},
                                       {NoteBlock::textBlock("three")},
                                       {NoteBlock::textBlock("last")}}));
    }

    void insertUpdateDeleteDrawings()
    {
        Note n = note("```wp id=d1\nbox a\n```\n\ntext");
        const QString end = n.insertDrawing("box z", 1);
        const QString after = n.insertDrawing("box b", 1, QString("d1"));
        QCOMPARE((QStringList{end, after}), (QStringList{"d2", "d3"}));
        QCOMPARE(n.pages()[0].blocks.size(), qsizetype(4));
        WP_EQ(n.pages()[0].blocks[1], drawing("d3", "box b"));

        n.updateDrawing("d3", "box c");
        QCOMPARE(n.drawing("d3")->drawing.source, QString("box c"));
        QCOMPARE(n.drawing("d3")->page, 1);

        n.deleteDrawing("d3");
        QVERIFY(!n.drawing("d3").has_value());
        QCOMPARE(n.nextDrawingID(), QString("d4"));
        const auto error = caught<NoteEditError>([&] { n.updateDrawing("d3", ""); });
        QVERIFY(error.has_value());
        WP_EQ(*error, NoteEditError::unknownDrawing("d3"));
        QVERIFY(caught<NoteEditError>([&] { n.insertDrawing("box", 1, QString("nope")); }).has_value());
    }

    void addAndRemovePages()
    {
        Note n = note("one");
        QCOMPARE(n.addPage(), 2);
        QCOMPARE(n.addPage(1), 2);
        QCOMPARE(n.pages().size(), qsizetype(3));
        n.removePage(2);
        n.removePage(2);
        const auto error = caught<NoteEditError>([&] { n.removePage(1); });
        QVERIFY(error.has_value());
        WP_EQ(*error, NoteEditError::lastPage());
    }

    void removingAPageRetiresItsDrawingIDs()
    {
        Note n = note("a\n+++page\n```wp id=d1\n```");
        n.removePage(2);
        QCOMPARE(n.nextDrawingID(), QString("d2"));
    }

    // MARK: Markdown export

    void markdownExport()
    {
        const Note n = note("---\ntitle: Plan\n---\nIntro\n\n```wp\nbox a\n```\n+++page\nPage two");
        QCOMPARE(n.markdown(), QString("# Plan\n\nIntro\n\n*[drawing omitted]*\n\n---\n\nPage two\n"));
        QCOMPARE(n.markdown(std::nullopt), QString("# Plan\n\nIntro\n\n---\n\nPage two\n"));
    }

    void markdownExportDoesNotRepeatTitleHeading()
    {
        QCOMPARE(note("---\ntitle: Plan\n---\n# Plan\nx").markdown(), QString("# Plan\nx\n"));
        QCOMPARE(note("").markdown(), QString(""));
    }
};

QTEST_APPLESS_MAIN(NoteEditingTests)
#include "NoteEditingTests.moc"
