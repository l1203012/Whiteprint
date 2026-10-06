#include "core/tests/TestSupport.h"
#include "study/StudyPlanRenderer.h"

using namespace wp;
using namespace wp::testing;

class StudyPlanRendererTests : public QObject
{
    Q_OBJECT

    static QString middot() { return QString::fromUtf8("\xc2\xb7"); }

    StudyPlan makePlan() const
    {
        const QString d = middot();
        StudyPlan plan;
        plan.title = "Networks 101";
        plan.overview = "Covers the TCP/IP stack.\nFocus on transport.";
        plan.modules = {
            StudyModule("Transport layer", 45,
                        {StudyPoint("Window size", Importance::good, "L3.pptx " + d + " slide 7"),
                         StudyPoint("History of TCP", Importance::skip, "L3.pptx " + d + " slide 1"),
                         StudyPoint("TCP is connection-oriented", Importance::must, "L3.pptx " + d + " slide 2"),
                         StudyPoint("Mascot slide", Importance::skip, "L3.pptx " + d + " slide 9")}),
            StudyModule("Routing", 90, {StudyPoint("Dijkstra", Importance::must, "L4.pdf " + d + " p. 3")}),
        };
        plan.tasks = {
            StudyTask("Read chapter 4", std::nullopt, "Syllabus.pdf " + d + " p. 2"),
            StudyTask("Lab 2", QString("week 6")),
            StudyTask("Essay", QString("2026-03-01"), "Syllabus.pdf " + d + " p. 6"),
            StudyTask("Quiz", QString("15.01.2026")),
        };
        plan.diagrams = {"flow App>TCP>IP", "  ", "box a \"Router\""};
        return plan;
    }

private slots:
    void pageOneHasOverviewAndLearningPath()
    {
        const Note note = StudyPlanRenderer::note(makePlan());
        WP_EQ(note.frontMatter.title(), std::optional<QString>(QString("Networks 101")));
        const QString expected = U(
            "# Networks 101\n\n"
            "## Overview \xc2\xb7 \xe2\x89\x88 2 h 15 min in total\n\n"
            "Covers the TCP/IP stack.\nFocus on transport.\n\n"
            "\xe2\x98\x85 must know \xc2\xb7 \xe2\x97\x8b good to know \xc2\xb7 \xe2\x9c\x95 can skip\n\n"
            "## Learning path\n\n"
            "### 1. Transport layer \xc2\xb7 \xe2\x89\x88 45 min\n\n"
            "- \xe2\x98\x85 **TCP is connection-oriented** \xe2\x80\x94 *L3.pptx \xc2\xb7 slide 2*\n"
            "- \xe2\x97\x8b Window size \xe2\x80\x94 *L3.pptx \xc2\xb7 slide 7*\n\n"
            "\xe2\x9c\x95 Can skip: History of TCP *(L3.pptx \xc2\xb7 slide 1)*; Mascot slide *(L3.pptx \xc2\xb7 slide 9)*\n\n"
            "### 2. Routing \xc2\xb7 \xe2\x89\x88 1 h 30 min\n\n"
            "- \xe2\x98\x85 **Dijkstra** \xe2\x80\x94 *L4.pdf \xc2\xb7 p. 3*");
        QCOMPARE(note.pages()[0].blocks.size(), qsizetype(1));
        QCOMPARE(note.pages()[0].blocks[0].text, expected);
    }

    void toDoIsSortedByDueDate()
    {
        const Note note = StudyPlanRenderer::note(makePlan());
        const QString expected = U(
            "## To-do\n\n"
            "- [ ] Quiz \xe2\x80\x94 due 15.01.2026\n"
            "- [ ] Essay \xe2\x80\x94 due 2026-03-01 \xe2\x80\x94 *Syllabus.pdf \xc2\xb7 p. 6*\n"
            "- [ ] Lab 2 \xe2\x80\x94 due week 6\n"
            "- [ ] Read chapter 4 \xe2\x80\x94 *Syllabus.pdf \xc2\xb7 p. 2*");
        QCOMPARE(note.pages()[1].blocks[0].text, expected);
    }

    void diagramsBecomeDrawings()
    {
        const Note note = StudyPlanRenderer::note(makePlan());
        QCOMPARE(note.pages().size(), qsizetype(3));
        QCOMPARE(note.pages()[2].blocks[0].text, QString("## Diagrams"));
        const QList<Drawing> expected = {Drawing("d1", "flow App>TCP>IP"), Drawing("d2", "box a \"Router\"")};
        WP_EQ(note.drawings(), expected);
    }

    void serializedNoteParsesBackTheSame()
    {
        const Note note = StudyPlanRenderer::note(makePlan());
        const QString text = note.serialized();
        QCOMPARE(text.split("\n+++page\n").size(), qsizetype(3));
        QVERIFY(text.contains("```wp id=d1\nflow App>TCP>IP\n```"));
        WP_EQ(Note::parsing(text), note);
    }

    void flashcardsGetTheirOwnPageBeforeTheDiagrams()
    {
        StudyPlan plan = makePlan();
        plan.flashcards = {
            Flashcard("What does TCP guarantee?", "Ordered,\nreliable delivery.", "L3.pptx " + middot() + " slide 2"),
            Flashcard(" ", "\n"),
            Flashcard("Q: tricky\n```wp", "A"),
        };
        const Note note = StudyPlanRenderer::note(plan);
        QCOMPARE(note.pages().size(), qsizetype(4));
        QCOMPARE(note.pages()[3].blocks[0].text, QString("## Diagrams"));
        QCOMPARE(note.pages()[2].blocks[0].text, QString("## Flashcards"));
        const QList<CardDeck> expected = {CardDeck("c1", QString("Networks 101"), {plan.flashcards[0], plan.flashcards[2]})};
        WP_EQ(note.decks(), expected);
        WP_EQ(Note::parsing(note.serialized()), note);
    }

    void noFlashcardsPageWithoutCards()
    {
        QVERIFY(StudyPlanRenderer::note(makePlan()).decks().isEmpty());
    }

    void emptyPagesAreLeftOut()
    {
        StudyPlan plan;
        plan.title = " ";
        const Note note = StudyPlanRenderer::note(plan);
        QCOMPARE(note.pages().size(), qsizetype(1));
        WP_EQ(note.frontMatter.title(), std::optional<QString>(QString("Study plan")));
        QVERIFY(!note.serialized().contains("in total"));
        QVERIFY(!note.serialized().contains("+++page"));
    }

    void freeTextCantBreakTheNoteStructure()
    {
        StudyPlan hostile;
        hostile.title = "T\n+++page";
        hostile.overview = "Intro\n+++page\n```wp\nbox x\n   ~~~\nend";
        hostile.modules = {StudyModule("M\n```wp", std::nullopt,
                                       {StudyPoint("a\n+++page", Importance::must, "r\n```"),
                                        StudyPoint("b", Importance::skip, "")})};
        hostile.tasks = {StudyTask("x\n+++page", QString("soon\n```wp"))};
        const Note note = StudyPlanRenderer::note(hostile);
        const Note reparsed = Note::parsing(note.serialized());
        WP_EQ(reparsed, note);
        QCOMPARE(reparsed.pages().size(), qsizetype(2));
        QVERIFY(reparsed.drawings().isEmpty());
        qsizetype separators = 0;
        for (const QString &line : note.serialized().split('\n'))
            separators += line == "+++page";
        QCOMPARE(separators, note.pages().size() - 1);
        QVERIFY(note.serialized().contains("\\+++page"));
        QVERIFY(note.serialized().contains(U("\xe2\x9c\x95 Can skip: b\n")));
    }

    void moduleWithoutMinutesHasNoEstimate()
    {
        StudyPlan plan("T", "o", {StudyModule("Only skips", std::nullopt, {StudyPoint("x", Importance::skip, "p. 1")})});
        const Note note = StudyPlanRenderer::note(plan);
        QVERIFY(note.pages()[0].blocks[0].text.endsWith(
            U("## Learning path\n\n### 1. Only skips\n\n\xe2\x9c\x95 Can skip: x *(p. 1)*")));
    }

    void datesAfterAMonthNameAndWithTimes()
    {
        StudyPlan plan("T", "o", {});
        plan.tasks = {StudyTask("c", QString("week 3")), StudyTask("b", QString("2026-01-15 23:59")),
                      StudyTask("a", QString("3 March 2026")), StudyTask("d", QString("Jan 2, 2026")), StudyTask("e")};
        const QString text = StudyPlanRenderer::note(plan).pages()[1].blocks[0].text;
        const QStringList lines = text.split('\n').mid(2);
        QCOMPARE(lines.size(), qsizetype(5));
        QVERIFY(lines[0].startsWith("- [ ] d"));
        QVERIFY(lines[1].startsWith("- [ ] b"));
        QVERIFY(lines[2].startsWith("- [ ] a"));
        QVERIFY(lines[3].startsWith("- [ ] c"));
        QVERIFY(lines[4].startsWith("- [ ] e"));
    }
};

QTEST_APPLESS_MAIN(StudyPlanRendererTests)
#include "StudyPlanRendererTests.moc"
