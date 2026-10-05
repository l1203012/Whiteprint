#include "TestSupport.h"
#include "core/Note.h"

using namespace wp;
using namespace wp::testing;

class NoteFormatTests : public QObject
{
    Q_OBJECT

private:
    static NoteBlock drawing(const char *id, const char *source)
    {
        return NoteBlock::drawingBlock(Drawing(id, source));
    }

    static QStringList drawingIDs(const Note &note)
    {
        QStringList ids;
        for (const auto &d : note.drawings())
            ids.append(d.id);
        return ids;
    }

private slots:
    void parsesFrontMatterPagesAndDrawings()
    {
        const Note note = Note::parsing(
            "---\n"
            "whiteprint: 1\n"
            "title: Network sketch\n"
            "---\n"
            "# Overview\n"
            "Some notes.\n"
            "\n"
            "```wp id=d1\n"
            "box a 0,0 \"Client\"\n"
            "```\n"
            "\n"
            "+++page\n"
            "# Page 2");

        WP_EQ(note.frontMatter.title(), std::optional<QString>(QString("Network sketch")));
        WP_EQ(note.frontMatter.formatVersion(), std::optional<int>(1));
        const QList<NotePage> expected = {
            NotePage({NoteBlock::textBlock("# Overview\nSome notes."), drawing("d1", "box a 0,0 \"Client\"")}),
            NotePage({NoteBlock::textBlock("# Page 2")}),
        };
        WP_EQ(note.pages(), expected);
    }

    void fileWithoutFrontMatterIsOneMarkdownPage()
    {
        const Note note = Note::parsing("Just text\n");
        WP_EQ(note.frontMatter.formatVersion(), std::optional<int>(Note::formatVersion));
        QVERIFY(!note.frontMatter.title().has_value());
        WP_EQ(note.pages(), (QList<NotePage>{NotePage({NoteBlock::textBlock("Just text")})}));
    }

    void emptyFileHasOneEmptyPage()
    {
        WP_EQ(Note::parsing("").pages(), (QList<NotePage>{NotePage()}));
    }

    void crlfLineEndingsAreNormalized()
    {
        const Note note = Note::parsing("---\r\ntitle: A\r\n---\r\nOne\r\n+++page\r\nTwo\r\n");
        WP_EQ(note.frontMatter.title(), std::optional<QString>(QString("A")));
        QCOMPARE(note.pages().size(), qsizetype(2));
        WP_EQ(note.pages()[0].blocks, (QList<NoteBlock>{NoteBlock::textBlock("One")}));
        WP_EQ(note.pages()[1].blocks, (QList<NoteBlock>{NoteBlock::textBlock("Two")}));
    }

    void frontMatterKeepsUnknownFieldsInOrderAndSplitsAtFirstColon()
    {
        const Note note = Note::parsing("---\ntags: a, b\ntitle: Time: 10:00\n---\n");
        QStringList keys;
        for (const auto &f : note.frontMatter.fields())
            keys.append(f.key);
        QCOMPARE(keys, (QStringList{"whiteprint", "tags", "title"}));
        WP_EQ(note.frontMatter.title(), std::optional<QString>(QString("Time: 10:00")));
        WP_EQ(note.frontMatter.value("tags"), std::optional<QString>(QString("a, b")));
    }

    void newerVersionIsRejected()
    {
        const auto error = caught<NoteFormatError>([] { Note::parsing("---\nwhiteprint: 2\n---\n"); });
        QVERIFY(error.has_value());
        WP_EQ(*error, NoteFormatError::unsupportedVersion(2));
    }

    void nonNumericVersionIsRejected()
    {
        const auto error = caught<NoteFormatError>([] { Note::parsing("---\nwhiteprint: one\n---\n"); });
        QVERIFY(error.has_value());
        WP_EQ(*error, NoteFormatError::invalidVersion("one"));
    }

    void structureInsideOtherCodeFencesIsPlainText()
    {
        const QString text = QStringLiteral("````markdown\n+++page\n```wp\nbox a\n```\n````");
        const Note note = Note::parsing(text);
        WP_EQ(note.pages(), (QList<NotePage>{NotePage({NoteBlock::textBlock(text)})}));
    }

    void tildeFenceCanHoldBacktickLines()
    {
        const QString text = QStringLiteral("~~~\n```wp\n+++page\n~~~");
        WP_EQ(Note::parsing(text).pages(), (QList<NotePage>{NotePage({NoteBlock::textBlock(text)})}));
    }

    void fourSpaceIndentedFenceIsNotAFence()
    {
        const Note note = Note::parsing("    ```wp\nbox a\n+++page\nnext");
        QCOMPARE(note.pages().size(), qsizetype(2));
        QVERIFY(note.drawings().isEmpty());
    }

    void unterminatedDrawingRunsToEndOfFile()
    {
        const Note note = Note::parsing("```wp id=d1\nbox a\n+++page\n");
        WP_EQ(note.pages(), (QList<NotePage>{NotePage({drawing("d1", "box a\n+++page\n")})}));
    }

    void missingAndDuplicateDrawingIDsAreAssigned()
    {
        const Note note = Note::parsing(
            "```wp\n"
            "box a\n"
            "```\n"
            "```wp id=d7\n"
            "box b\n"
            "```\n"
            "```wp id=d7\n"
            "box c\n"
            "```\n"
            "```wp id=has space\n"
            "box d\n"
            "```");
        QCOMPARE(drawingIDs(note), (QStringList{"d8", "d7", "d9", "has"}));
        QCOMPARE(note.nextDrawingID(), QString("d10"));
    }

    void drawingIDsAreNotReusedAfterDeletion()
    {
        Note note = Note::parsing("```wp id=d1\n```\n```wp id=d2\n```");
        note.deleteDrawing("d2");
        QCOMPARE(note.nextDrawingID(), QString("d3"));
        const Note reloaded = Note::parsing(note.serialized());
        QCOMPARE(reloaded.nextDrawingID(), QString("d3"));
    }

    void nextDrawingIDStartsAtOne()
    {
        QCOMPARE(Note().nextDrawingID(), QString("d1"));
    }

    void pagesCanNeverBeEmpty()
    {
        Note note{QList<NotePage>{}};
        WP_EQ(note.pages(), (QList<NotePage>{NotePage()}));
        note.setPages({});
        WP_EQ(note.pages(), (QList<NotePage>{NotePage()}));
    }

    void versionFieldCannotBeChangedOrRemoved()
    {
        FrontMatter frontMatter;
        frontMatter.setValue("whiteprint", QString("9"));
        frontMatter.setValue("whiteprint", std::nullopt);
        WP_EQ(frontMatter.formatVersion(), std::optional<int>(Note::formatVersion));
    }

    void frontMatterValuesStaySingleLine()
    {
        FrontMatter frontMatter;
        frontMatter.setTitle(QString("Two\nlines"));
        WP_EQ(frontMatter.title(), std::optional<QString>(QString("Two lines")));
        frontMatter.setTitle(std::nullopt);
        QVERIFY(!frontMatter.title().has_value());
    }

    // MARK: Serializing

    void serializesCanonicalForm()
    {
        const Note note(QString("Sketch"), {NotePage({NoteBlock::textBlock("# One"), drawing("d1", "box a")}),
                                            NotePage({NoteBlock::textBlock("Two")})});
        QCOMPARE(note.serialized(), QString(
            "---\n"
            "whiteprint: 1\n"
            "title: Sketch\n"
            "last-drawing: 1\n"
            "---\n"
            "\n"
            "# One\n"
            "\n"
            "```wp id=d1\n"
            "box a\n"
            "```\n"
            "\n"
            "+++page\n"
            "\n"
            "Two\n"));
    }

    void roundTrips()
    {
        const QList<Note> notes = {
            Note(),
            Note(QString("T"), {NotePage(), NotePage(), NotePage({NoteBlock::textBlock("x")})}),
            Note(QList<NotePage>{NotePage({
                drawing("d1", ""),
                drawing("d2", "\n"),
                drawing("d3", "box a\n```\nbox b"),
                NoteBlock::textBlock("Para one\n\nPara two\n```swift\nlet x = 1\n```"),
                drawing("d4", "flow a>b"),
            })}),
        };
        for (const Note &note : notes)
            WP_EQ(Note::parsing(note.serialized()), note);
    }

    void serializingIsIdempotentForMessyInput()
    {
        const QString messy = QStringLiteral("\n\n# Hi   \n\n\n```wp\nbox a\n```\n+++page  \n\n\ntext\n");
        const QString once = Note::parsing(messy).serialized();
        QCOMPARE(Note::parsing(once).serialized(), once);
    }

    void drawingContainingFencesGetsLongerFence()
    {
        const Drawing d("d1", "text \"x\"\n````\n```");
        const QString text = Note(QList<NotePage>{NotePage({NoteBlock::drawingBlock(d)})}).serialized();
        QVERIFY(text.contains(QLatin1String("`````wp id=d1\n")));
        WP_EQ(Note::parsing(text).drawings(), (QList<Drawing>{d}));
    }

    void textWithUnclosedFenceIsClosedSoLaterBlocksSurvive()
    {
        const Note note(QList<NotePage>{NotePage({NoteBlock::textBlock("```\ncode"), drawing("d1", "box a")})});
        const Note parsed = Note::parsing(note.serialized());
        WP_EQ(parsed.pages()[0].blocks, (QList<NoteBlock>{NoteBlock::textBlock("```\ncode\n```"), drawing("d1", "box a")}));
    }
};

QTEST_APPLESS_MAIN(NoteFormatTests)
#include "NoteFormatTests.moc"
