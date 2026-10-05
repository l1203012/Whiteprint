// Port of BridgeServiceTests.swift.
#include "app/AgentToolBridge.h"
#include "app/AppUtil.h"
#include "app/BridgeService.h"
#include "app/NoteRegistry.h"
#include "app/NotesFolder.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>
#include <future>

using namespace wp;

namespace {

/// Notes held in memory, keyed by file path, as the bridge sees them.
class MemoryWorkspace : public NoteWorkspace {
public:
    struct Item {
        QString url;
        Note note;
    };
    QList<Item> notes;
    QStringList edits;
    QString folder = canonicalFile(QDir::tempPath() + "/whiteprint-tests");

    QString add(const QString &name, const QString &text)
    {
        const QString url = canonicalFile(folder + "/" + name);
        notes.append({url, Note::parsing(text)});
        return url;
    }

    int indexOf(const QString &url) const
    {
        for (int i = 0; i < notes.size(); ++i)
            if (pathKey(notes[i].url) == pathKey(url))
                return i;
        return -1;
    }

    QStringList noteURLs() override
    {
        QStringList result;
        for (const Item &item : notes)
            result << item.url;
        return result;
    }

    Note note(const QString &url) override
    {
        const int i = indexOf(url);
        if (i < 0)
            throw WorkspaceError::fileNotFound(url);
        return notes[i].note;
    }

    void edit(const QString &noteAt, const QString &actionName, const std::function<void(Note &)> &change) override
    {
        const int i = indexOf(noteAt);
        if (i < 0)
            throw WorkspaceError::fileNotFound(noteAt);
        Note edited = notes[i].note;
        change(edited);
        notes[i].note = edited;
        edits << actionName;
    }

    std::optional<QString> folderPath(const QString &of) override { return NotesFolder(folder).relativeFolder(of); }

    QString createNote(const Note &note, const QString &title, const std::optional<QString> &path) override
    {
        const QString directory = path ? NotesFolder(folder).folderURL(*path) : canonicalFile(folder);
        const QString url = directory + "/" + title + ".wprint";
        notes.append({url, note});
        return url;
    }
};

/// Records requests and answers them from a table.
class FakeHandler : public BridgeHandler {
public:
    QList<BridgeRequest> requests;
    BridgeResponse response = BridgeResponse::ok("ok");

    void handle(const BridgeRequest &request, std::function<void(BridgeResponse)> reply) override
    {
        requests.append(request);
        reply(response);
    }
};

} // namespace

class BridgeServiceTests : public QObject {
    Q_OBJECT

    MemoryWorkspace m_workspace;
    NoteRegistry m_registry;
    std::unique_ptr<QTemporaryDir> m_studyDir;
    std::unique_ptr<StudyStore> m_store;
    std::unique_ptr<BridgeService> m_service;

    BridgeResponse send(const BridgeRequest &request)
    {
        auto promise = std::make_shared<std::promise<BridgeResponse>>();
        auto future = promise->get_future();
        m_service->handle(request, [promise](BridgeResponse response) { promise->set_value(std::move(response)); });
        return future.get();
    }

    QString ok(const BridgeRequest &request)
    {
        const BridgeResponse response = send(request);
        if (!response.isOk)
            qWarning("expected ok, got failure: %s", qPrintable(response.text));
        return response.text;
    }

    QString listedID(const QString &title)
    {
        for (const QString &line : ok(BridgeRequest::listNotes()).split('\n'))
            if (line.contains("\"" + title + "\""))
                return line.split(' ').first();
        return {};
    }

    static BridgeResponse failure(const QString &text) { return BridgeResponse::failure(text); }

private slots:
    void init()
    {
        m_workspace.notes.clear(); m_workspace.edits.clear();
        m_registry = NoteRegistry();
        m_studyDir = std::make_unique<QTemporaryDir>();
        m_store = std::make_unique<StudyStore>(m_studyDir->path() + "/study");
        m_service = std::make_unique<BridgeService>(&m_workspace, &m_registry, m_store.get());
    }

    void cleanup()
    {
        m_service.reset();
        m_store.reset();
        m_studyDir.reset();
    }

    void listNotesAssignsStableIDs()
    {
        m_workspace.add("Alpha.wprint", "---\nwhiteprint: 1\ntitle: Networks\n---\nOne\n+++page\nTwo");
        m_workspace.add("Beta.wprint", "Hello");
        QCOMPARE(ok(BridgeRequest::listNotes()), QStringLiteral("n1 \"Networks\" · 2 pages\nn2 \"Beta\" · 1 page"));
        std::reverse(m_workspace.notes.begin(), m_workspace.notes.end());
        QCOMPARE(ok(BridgeRequest::listNotes()), QStringLiteral("n2 \"Beta\" · 1 page\nn1 \"Networks\" · 2 pages"));
    }

    void listNotesWhenEmpty() { QCOMPARE(ok(BridgeRequest::listNotes()), QStringLiteral("no notes")); }

    void readNoteShowsPageHeadersForAllPages()
    {
        m_workspace.add("A.wprint", "One\n\n```wp\nbox a\n```\n+++page\nTwo");
        const QString id = listedID("A");
        QCOMPARE(ok(BridgeRequest::readNote(id)),
                 QStringLiteral("=== page 1 ===\nOne\n\n```wp id=d1\nbox a\n```\n\n=== page 2 ===\nTwo"));
        QCOMPARE(ok(BridgeRequest::readNote(id, 2)), QStringLiteral("Two"));
    }

    void unknownNoteAndPageFailWithShortErrors()
    {
        m_workspace.add("A.wprint", "One");
        const QString id = listedID("A");
        QVERIFY(send(BridgeRequest::readNote("n9")) == failure("no note 'n9' (see list_notes)"));
        QVERIFY(send(BridgeRequest::write(id, 3, "x", WriteMode::append)) == failure("page 3 doesn't exist (note has 1 page)"));
    }

    void writeGoesThroughTheWorkspace()
    {
        const QString url = m_workspace.add("A.wprint", "One");
        const QString id = listedID("A");
        QCOMPARE(ok(BridgeRequest::write(id, 1, "Two", WriteMode::append)), QStringLiteral("ok"));
        const auto blocks = m_workspace.note(url).pages()[0].blocks;
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].text, QStringLiteral("One"));
        QCOMPARE(blocks[1].text, QStringLiteral("Two"));
        QCOMPARE(m_workspace.edits.size(), 1);
    }

    void drawReturnsIDAndCompileErrors()
    {
        const QString url = m_workspace.add("A.wprint", "One");
        const QString id = listedID("A");
        QCOMPARE(ok(BridgeRequest::draw(id, 1, "flow a>b")), QStringLiteral("ok d1"));
        const QStringList lines = ok(BridgeRequest::draw(id, 1, "box a\narrow a>x", QString("d1"))).split('\n');
        QCOMPARE(lines.first(), QStringLiteral("ok d2"));
        QCOMPARE(lines.size(), 2);
        QVERIFY2(lines[1].startsWith("line 2: "), qPrintable(lines[1]));
        const auto drawings = m_workspace.note(url).drawings();
        QCOMPARE(drawings.size(), 2);
        QCOMPARE(drawings[0].id, QStringLiteral("d1"));
        QCOMPARE(drawings[1].id, QStringLiteral("d2"));
    }

    void drawingWhereNothingCompilesIsNotSaved()
    {
        const QString url = m_workspace.add("A.wprint", "```wp id=d1\nbox a\n```");
        const QString id = listedID("A");
        QVERIFY(send(BridgeRequest::draw(id, 1, "flow a>"))
                == failure("nothing to draw, not saved:\nline 1: bad link 'a>', use a>b"));
        QVERIFY(send(BridgeRequest::editDrawing(id, "d1", "squiggle"))
                == failure("nothing to draw, not saved:\nline 1: unknown command 'squiggle'"));
        const auto drawings = m_workspace.note(url).drawings();
        QCOMPARE(drawings.size(), 1);
        QVERIFY(drawings[0] == Drawing("d1", "box a"));
    }

    void editAndDeleteDrawing()
    {
        const QString url = m_workspace.add("A.wprint", "```wp id=d1\nbox a\n```");
        const QString id = listedID("A");
        QCOMPARE(ok(BridgeRequest::editDrawing(id, "d1", "box b")), QStringLiteral("ok"));
        QCOMPARE(m_workspace.note(url).drawing("d1")->drawing.source, QStringLiteral("box b"));
        QCOMPARE(ok(BridgeRequest::deleteDrawing(id, "d1")), QStringLiteral("ok"));
        QVERIFY(send(BridgeRequest::deleteDrawing(id, "d1")) == failure("no drawing 'd1' in this note"));
    }

    void addPageReturnsItsNumber()
    {
        m_workspace.add("A.wprint", "One");
        QCOMPARE(ok(BridgeRequest::addPage(listedID("A"))), QStringLiteral("ok 2"));
    }

    void createNoteReturnsNewID()
    {
        QCOMPARE(ok(BridgeRequest::createNote("Plan", QString("# Plan\n+++page\nMore"))), QStringLiteral("ok n1"));
        const Note note = m_workspace.note(*m_registry.url("n1"));
        QCOMPARE(*note.frontMatter.title(), QStringLiteral("Plan"));
        QCOMPARE(note.pages().size(), 2);
    }

    void listNotesShowsFolders()
    {
        m_workspace.add("Top.wprint", "One");
        m_workspace.add("Courses/Networks/TCP.wprint", "One\n+++page\nTwo");
        QCOMPARE(ok(BridgeRequest::listNotes()),
                 QStringLiteral("n1 \"Top\" · 1 page\nn2 \"TCP\" · Courses/Networks · 2 pages"));
    }

    void createNoteInFolder()
    {
        QCOMPARE(ok(BridgeRequest::createNote("TCP", std::nullopt, QString("Courses/Networks"))), QStringLiteral("ok n1"));
        QCOMPARE(*m_workspace.folderPath(*m_registry.url("n1")), QStringLiteral("Courses/Networks"));
        for (const QString &bad : {"../Elsewhere", "/etc", "Courses/../../x", "~/Notes"}) {
            const BridgeResponse refused = send(BridgeRequest::createNote("X", std::nullopt, bad));
            QVERIFY2(!refused.isOk, qPrintable(bad + " was accepted"));
            QVERIFY2(refused.text.contains("must be a path inside the notes folder"), qPrintable(refused.text));
        }
    }

    void createFlashcardsMakesANewNote()
    {
        const QList<Flashcard> cards{Flashcard("What does TCP guarantee?", "Ordered delivery", QString("Lecture3.pptx · slide 4"))};
        QCOMPARE(ok(BridgeRequest::createFlashcards(std::nullopt, std::nullopt, "TCP", cards)), QStringLiteral("ok n1 c1"));
        const QString url = *m_registry.url("n1");
        QCOMPARE(QFileInfo(url).fileName(), QStringLiteral("Flashcards – TCP.wprint"));
        const auto decks = m_workspace.note(url).decks();
        QCOMPARE(decks.size(), 1);
        QVERIFY(decks[0] == CardDeck("c1", QString("TCP"), cards));
    }

    void createFlashcardsAddsToTheLastPageByDefault()
    {
        const QString url = m_workspace.add("A.wprint", "One\n+++page\nTwo");
        const QString id = listedID("A");
        const QList<Flashcard> cards{Flashcard("Q", "A")};
        QCOMPARE(ok(BridgeRequest::createFlashcards(id, std::nullopt, "Deck", cards)), QStringLiteral("ok %1 c1").arg(id));
        QCOMPARE(ok(BridgeRequest::createFlashcards(id, 1, "Other", cards)), QStringLiteral("ok %1 c2").arg(id));
        const Note note = m_workspace.note(url);
        QCOMPARE(note.deck("c1")->page, 2);
        QCOMPARE(note.deck("c2")->page, 1);
        QVERIFY(send(BridgeRequest::createFlashcards(id, std::nullopt, "Empty", {}))
                == failure("no cards given; each card needs a question and an answer"));
        QVERIFY(send(BridgeRequest::createFlashcards(id, 7, "Deck", cards)) == failure("page 7 doesn't exist (note has 2 pages)"));
    }

    void importValidatesPathAndType()
    {
        QVERIFY(send(BridgeRequest::importDocument("/nonexistent/Lecture.pdf")) == failure("no file at /nonexistent/Lecture.pdf"));
        QTemporaryDir dir;
        const QString text = dir.path() + "/wp-test.txt";
        { QFile f(text); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("hi"); }
        QVERIFY(send(BridgeRequest::importDocument(text))
                == failure("wp-test.txt: unsupported file type (use PDF, DOCX, DOC or PPTX)"));
    }

    void buildStudyPlanRejectsUnknownImports()
    {
        const StudyPlan plan("Networks", "", {});
        QVERIFY(send(BridgeRequest::buildStudyPlan({"i42"}, plan)) == failure("no import 'i42'"));
    }

    void buildStudyPlanCreatesNote()
    {
        const StudyPlan plan("Networks", "Covers routing.", {});
        QCOMPARE(ok(BridgeRequest::buildStudyPlan({}, plan)), QStringLiteral("ok n1"));
        QCOMPARE(QFileInfo(*m_registry.url("n1")).fileName(), QStringLiteral("Study plan – Networks.wprint"));
    }

    void importSummary()
    {
        const StudyImport item("i2", "Lecture3.pptx", 14, 3, 0, QDateTime::currentDateTime());
        QCOMPARE(BridgeService::summary(item), QStringLiteral("i2 · 14 slides · 3 chunks"));
        const StudyImport pdf("i3", "Syllabus.PDF", 1, 1, 1, QDateTime::currentDateTime());
        QCOMPARE(BridgeService::summary(pdf, false), QStringLiteral("1 page · 1 chunk"));
    }

    void listImportsAndPointsRunOffThread()
    {
        QCOMPARE(ok(BridgeRequest::listImports()), QStringLiteral("no imports"));
        QCOMPARE(ok(BridgeRequest::getPoints()), QStringLiteral("no imports"));
    }

    void ping() { QCOMPARE(ok(BridgeRequest::ping()), QStringLiteral("pong")); }

    // AgentToolBridge
    void mapsToolsOneToOne()
    {
        AgentTool tool;
        tool.name = "list_notes";
        tool.description = "List notes";
        tool.inputSchema = QJsonObject{{"type", "object"}};
        const auto mapped = AgentToolBridge::runnerTools({tool});
        QCOMPARE(mapped.size(), 1);
        QCOMPARE(mapped[0].name, QStringLiteral("list_notes"));
        QCOMPARE(mapped[0].description, QStringLiteral("List notes"));
        QCOMPARE(mapped[0].inputSchema["type"].toString(), QStringLiteral("object"));
        QVERIFY(AgentToolBridge::runnerTools().size() > 5);
    }

    void executorRunsRequestsThroughTheHandler()
    {
        FakeHandler handler;
        const auto executor = AgentToolBridge::executor(&handler, [](const QString &name, const QJsonObject &arguments) {
            if (name != "read_note" || !arguments.contains("note"))
                throw WorkspaceError::unknownNote(name);
            return BridgeRequest::readNote(arguments["note"].toString());
        });
        const auto run = [&](const QString &name, const QJsonObject &arguments) {
            QString text;
            bool isError = false;
            executor(name, arguments, [&](const QString &t, bool e) { text = t; isError = e; });
            return std::make_pair(text, isError);
        };
        auto result = run("read_note", {{"note", "n1"}});
        QCOMPARE(result.first, QStringLiteral("ok"));
        QVERIFY(!result.second);
        QCOMPARE(handler.requests.size(), 1);
        QVERIFY(handler.requests[0] == BridgeRequest::readNote("n1"));

        handler.response = BridgeResponse::failure("no note 'n1' (see list_notes)");
        result = run("read_note", {{"note", "n1"}});
        QCOMPARE(result.first, QStringLiteral("no note 'n1' (see list_notes)"));
        QVERIFY(result.second);

        result = run("bogus", {});
        QCOMPARE(result.first, QStringLiteral("no note 'bogus' (see list_notes)"));
        QVERIFY(result.second);
        QCOMPARE(handler.requests.size(), 2);
    }
};

QTEST_GUILESS_MAIN(BridgeServiceTests)
#include "BridgeServiceTests.moc"
