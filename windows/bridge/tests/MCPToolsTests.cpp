#include "BridgeTestSupport.h"
#include "bridge/MCPServer.h"

using namespace wp;
using namespace wp::testing;

class MCPToolsTests : public QObject
{
    Q_OBJECT

    QList<BridgeRequest> sent;
    MCPServer *server = nullptr;

    struct Reply {
        QString text;
        bool isError = false;
    };

    /// Calls a tool through JSON-RPC; `arguments` is raw JSON.
    Reply call(const QString &name, const QString &arguments)
    {
        const QString line = QStringLiteral(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"%1","arguments":%2}})")
                                 .arg(name, arguments);
        const auto reply = server->handle(line);
        if (!reply)
            return {"<no reply>", true};
        const QJsonObject result = parseObject(*reply).value("result").toObject();
        const QJsonObject first = result.value("content").toArray().at(0).toObject();
        return {first.value("text").toString(), result.value("isError").toBool()};
    }

    std::optional<BridgeRequest> request(const QString &name, const QString &arguments)
    {
        sent.clear();
        const Reply r = call(name, arguments);
        if (r.isError || r.text != "done")
            QTest::qFail(qPrintable(QStringLiteral("%1: %2").arg(name, r.text)), __FILE__, __LINE__);
        if (sent.isEmpty())
            return std::nullopt;
        return sent.first();
    }

    QString invalid(const QString &name, const QString &arguments)
    {
        sent.clear();
        const Reply r = call(name, arguments);
        if (!r.isError)
            QTest::qFail(qPrintable(QStringLiteral("%1 accepted %2").arg(name, arguments)), __FILE__, __LINE__);
        CHECK2(sent.isEmpty(), "nothing may be sent");
        CHECK2(!r.text.contains('\n'), "error must be one line");
        return r.text;
    }

    QJsonArray toolList()
    {
        const auto reply = server->handle(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
        return parseObject(reply.value_or(QString())).value("result").toObject().value("tools").toArray();
    }

    // The checks below run inside helper functions, so failures return early via these macros.
#define REQ(name, args, expected)                                                                                  \
    do {                                                                                                           \
        const auto actual_ = request(name, args);                                                                  \
        QVERIFY2(actual_.has_value(), name);                                                                       \
        QVERIFY2((*actual_ == (expected)), qPrintable(QStringLiteral("%1: %2").arg(name, actual_->description()))); \
    } while (false)

    void checkSchema(const QJsonObject &schema, const QString &path, bool root = false)
    {
        const QString type = schema.value("type").toString();
        QVERIFY2(QStringList({"object", "array", "string", "integer"}).contains(type), qPrintable(path));
        if (root)
            QVERIFY2(type == "object", qPrintable(path));
        if (type == "object") {
            const QJsonObject properties = schema.value("properties").toObject();
            QVERIFY2(schema.value("properties").isObject(), qPrintable(path));
            for (auto it = properties.begin(); it != properties.end(); ++it)
                checkSchema(it.value().toObject(), path + "." + it.key());
            for (const QJsonValue &name : schema.value("required").toArray())
                QVERIFY2(properties.contains(name.toString()), qPrintable(path + " requires undeclared " + name.toString()));
        } else if (type == "array") {
            QVERIFY2(schema.value("items").isObject(), qPrintable(path));
            checkSchema(schema.value("items").toObject(), path + "[]");
        } else if (schema.contains("enum")) {
            QVERIFY2(!schema.value("enum").toArray().isEmpty(), qPrintable(path));
        }
    }

private slots:
    void init()
    {
        sent.clear();
        delete server;
        server = new MCPServer([this](const BridgeRequest &r) {
            sent.append(r);
            return BridgeResponse::ok("done");
        });
    }

    void cleanup()
    {
        delete server;
        server = nullptr;
    }

    // MARK: tools/list

    void listsExactlyTheFifteenTools()
    {
        QStringList names;
        for (const QJsonValue &tool : toolList())
            names.append(tool.toObject().value("name").toString());
        QCOMPARE(names, (QStringList{"list_notes", "read_note", "create_note", "write", "draw", "edit_drawing",
                                     "delete_drawing", "add_page", "import_document", "list_imports", "read_chunk",
                                     "save_points", "get_points", "build_study_plan", "create_flashcards"}));
    }

    void toolListMatchesTheCatalog()
    {
        const QJsonArray listed = toolList();
        const QList<AgentTool> catalog = MCPToolCatalog::tools();
        QCOMPARE(listed.size(), catalog.size());
        for (int i = 0; i < listed.size(); ++i) {
            const QJsonObject json = listed.at(i).toObject();
            const AgentTool &tool = catalog.at(i);
            QCOMPARE(json.value("name").toString(), tool.name);
            QCOMPARE(json.value("description").toString(), tool.description);
            QJsonObject annotations;
            for (auto it = tool.annotations.begin(); it != tool.annotations.end(); ++it)
                annotations.insert(it.key(), it.value());
            QVERIFY2(json.value("annotations").toObject() == annotations, qPrintable(tool.name));
            QVERIFY2(json.value("inputSchema").toObject() == tool.inputSchema, qPrintable(tool.name));
        }
    }

    void toolListSize()
    {
        const auto reply = server->handle(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
        QVERIFY(reply.has_value());
        QVERIFY(reply->toUtf8().size() < 6000);
    }

    void schemasAreWellFormed()
    {
        for (const QJsonValue &value : toolList()) {
            const QJsonObject tool = value.toObject();
            const QString name = tool.value("name").toString();
            const QString description = tool.value("description").toString();
            QVERIFY2(!description.isEmpty(), qPrintable(name));
            QVERIFY2(description.size() <= 90, qPrintable(name));
            QVERIFY2(!tool.value("annotations").toObject().isEmpty(), qPrintable(name));
            checkSchema(tool.value("inputSchema").toObject(), name, true);
            if (QTest::currentTestFailed())
                return;
        }
    }

    void annotations()
    {
        QMap<QString, QJsonObject> hints;
        for (const QJsonValue &tool : toolList())
            hints.insert(tool.toObject().value("name").toString(), tool.toObject().value("annotations").toObject());
        for (const QString name : {"list_notes", "read_note", "list_imports", "read_chunk", "get_points"})
            QVERIFY2((hints[name] == QJsonObject{{"readOnlyHint", true}}), qPrintable(name));
        for (const QString name : {"write", "edit_drawing", "delete_drawing"})
            QVERIFY2((hints[name] == QJsonObject{{"destructiveHint", true}}), qPrintable(name));
        for (const QString name : {"create_note", "draw", "add_page", "import_document", "save_points",
                                   "build_study_plan", "create_flashcards"})
            QVERIFY2((hints[name] == QJsonObject{{"destructiveHint", false}}), qPrintable(name));
    }

    void drawPointsToTheDSLResource()
    {
        for (const QJsonValue &tool : toolList()) {
            if (tool.toObject().value("name").toString() == "draw") {
                QVERIFY(tool.toObject().value("description").toString().contains("whiteprint://dsl"));
                return;
            }
        }
        QFAIL("no draw tool");
    }

    // MARK: argument -> request mapping

    void noteTools()
    {
        REQ("list_notes", "{}", BridgeRequest::listNotes());
        REQ("read_note", R"({"note":"n1"})", BridgeRequest::readNote("n1"));
        REQ("read_note", R"({"note":"n1","page":2})", BridgeRequest::readNote("n1", 2));
        REQ("create_note", R"({"title":"T"})", BridgeRequest::createNote("T"));
        REQ("create_note", R"({"title":"T","markdown":"# Hi"})", BridgeRequest::createNote("T", QString("# Hi")));
        REQ("write", R"({"note":"n1","page":1,"markdown":"x","mode":"append"})",
            BridgeRequest::write("n1", 1, "x", WriteMode::append));
        REQ("write", R"({"note":"n1","page":3,"markdown":"","mode":"replace"})",
            BridgeRequest::write("n1", 3, "", WriteMode::replace));
        REQ("draw", R"({"note":"n1","page":1,"dsl":"box a"})", BridgeRequest::draw("n1", 1, "box a"));
        REQ("draw", R"({"note":"n1","page":1,"dsl":"box a","after":"d2"})",
            BridgeRequest::draw("n1", 1, "box a", QString("d2")));
        REQ("edit_drawing", R"({"note":"n1","drawing":"d1","dsl":"box b"})", BridgeRequest::editDrawing("n1", "d1", "box b"));
        REQ("delete_drawing", R"({"note":"n1","drawing":"d1"})", BridgeRequest::deleteDrawing("n1", "d1"));
        REQ("add_page", R"({"note":"n1"})", BridgeRequest::addPage("n1"));
    }

    void studyTools()
    {
        REQ("import_document", R"({"path":"/tmp/a.pdf"})", BridgeRequest::importDocument("/tmp/a.pdf"));
        REQ("list_imports", "{}", BridgeRequest::listImports());
        REQ("read_chunk", R"({"import":"i1","chunk":0})", BridgeRequest::readChunk("i1", 0));
        REQ("get_points", "{}", BridgeRequest::getPoints());
        REQ("get_points", R"({"import":"i2"})", BridgeRequest::getPoints(QString("i2")));
        const QString dot = U("\xc2\xb7");
        const QString points = QStringLiteral(
            R"([{"text":"TCP is reliable","importance":"must","ref":"L1.pdf %1 p. 3","topic":"Transport"},)"
            R"({"text":"History of ARPANET","importance":"skip","ref":"L1.pdf %1 p. 1"}])").arg(dot);
        REQ("save_points", QStringLiteral(R"({"import":"i1","chunk":2,"points":%1})").arg(points),
            BridgeRequest::savePoints("i1", 2,
                                      {StudyPoint("TCP is reliable", Importance::must, "L1.pdf " + dot + " p. 3", QString("Transport")),
                                       StudyPoint("History of ARPANET", Importance::skip, "L1.pdf " + dot + " p. 1")}));
        REQ("save_points", R"({"import":"i1","chunk":3,"points":[]})", BridgeRequest::savePoints("i1", 3, {}));
    }

    void buildStudyPlan()
    {
        const QString dot = U("\xc2\xb7");
        const QString plan = QStringLiteral(
            R"({"title":"Networks","overview":"Layers.","modules":[)"
            R"({"title":"Transport","minutes":45,"points":[{"text":"TCP","importance":"must","ref":"L1 %1 p. 3"}]},)"
            R"({"title":"Extras","points":[]}],)"
            R"("tasks":[{"text":"Lab 1","due":"week 2","ref":"Syllabus %1 p. 2"},{"text":"Read ch. 3"}],)"
            R"("diagrams":["flow A>B"]})").arg(dot);
        REQ("build_study_plan", QStringLiteral(R"({"imports":["i1","i2"],"plan":%1})").arg(plan),
            BridgeRequest::buildStudyPlan(
                {"i1", "i2"},
                StudyPlan("Networks", "Layers.",
                          {StudyModule("Transport", 45, {StudyPoint("TCP", Importance::must, "L1 " + dot + " p. 3")}),
                           StudyModule("Extras", std::nullopt, {})},
                          {StudyTask("Lab 1", QString("week 2"), "Syllabus " + dot + " p. 2"), StudyTask("Read ch. 3")},
                          {"flow A>B"})));
    }

    void studyPlanTasksAndDiagramsAreOptional()
    {
        REQ("build_study_plan", R"({"imports":[],"plan":{"title":"T","overview":"O","modules":[]}})",
            BridgeRequest::buildStudyPlan({}, StudyPlan("T", "O", {})));
    }

    void createNoteInFolder()
    {
        REQ("create_note", R"({"title":"T","folder":"Courses/Networks"})",
            BridgeRequest::createNote("T", std::nullopt, QString("Courses/Networks")));
        REQ("create_note", R"({"title":"T","folder":" Courses/ "})", BridgeRequest::createNote("T", std::nullopt, QString("Courses")));
        REQ("create_note", R"({"title":"T","folder":""})", BridgeRequest::createNote("T"));
        for (const QString folder : {"/Users/x", "~/Notes", "../Secrets", "a/../../b"}) {
            QCOMPARE(invalid("create_note", QStringLiteral(R"({"title":"T","folder":"%1"})").arg(folder)),
                     QStringLiteral("Invalid arguments: folder: use a path inside the notes folder, e.g. Courses/Networks"));
        }
    }

    void createFlashcards()
    {
        const QString dot = U("\xc2\xb7");
        const QString cards = QStringLiteral(
            R"([{"question":"What does TCP guarantee?","answer":"Ordered, reliable delivery.","ref":"L3.pptx %1 slide 4"},)"
            R"({"question":"UDP?","answer":"Datagrams."}])").arg(dot);
        const QList<Flashcard> expected{Flashcard("What does TCP guarantee?", "Ordered, reliable delivery.", "L3.pptx " + dot + " slide 4"),
                                        Flashcard("UDP?", "Datagrams.")};
        REQ("create_flashcards", QStringLiteral(R"({"title":"TCP","cards":%1})").arg(cards),
            BridgeRequest::createFlashcards(std::nullopt, std::nullopt, "TCP", expected));
        REQ("create_flashcards", QStringLiteral(R"({"note":"n4","page":"2","title":"TCP","cards":%1})").arg(cards),
            BridgeRequest::createFlashcards(QString("n4"), 2, "TCP", expected));
        QCOMPARE(invalid("create_flashcards", R"({"title":"TCP","cards":[]})"),
                 QStringLiteral("Invalid arguments: cards: add at least one card"));
        QCOMPARE(invalid("create_flashcards", R"({"title":"TCP","cards":[{"question":"Q"}]})"),
                 QStringLiteral("Invalid arguments: cards[0].answer: required"));
        QCOMPARE(invalid("create_flashcards", QStringLiteral(R"({"cards":%1})").arg(cards)),
                 QStringLiteral("Invalid arguments: title: required"));
    }

    void studyPlanFlashcards()
    {
        REQ("build_study_plan",
            R"({"imports":["i1"],"plan":{"title":"T","overview":"O","modules":[],"flashcards":[{"question":"Q","answer":"A","ref":"p. 1"},{"question":"Q2","answer":"A2"}]}})",
            BridgeRequest::buildStudyPlan({"i1"}, StudyPlan("T", "O", {}, {}, {},
                                                            {Flashcard("Q", "A", QString("p. 1")), Flashcard("Q2", "A2")})));
        QCOMPARE(invalid("build_study_plan",
                         R"({"imports":[],"plan":{"title":"T","overview":"O","modules":[],"flashcards":[{"question":"Q"}]}})"),
                 QStringLiteral("Invalid arguments: plan.flashcards[0].answer: required"));
    }

    // MARK: catalog

    void catalogMapsLikeToolsCall()
    {
        const QList<QPair<QString, QString>> calls = {
            {"list_notes", "{}"},
            {"read_note", R"({"note":"n1","page":2})"},
            {"create_note", R"({"title":"T","folder":"A"})"},
            {"write", R"({"note":"n1","page":1,"markdown":"x","mode":"replace"})"},
            {"draw", R"({"note":"n1","page":1,"dsl":"box a"})"},
            {"edit_drawing", R"({"note":"n1","drawing":"d1","dsl":"x"})"},
            {"delete_drawing", R"({"note":"n1","drawing":"d1"})"},
            {"add_page", R"({"note":"n1"})"},
            {"import_document", R"({"path":"/tmp/a.pdf"})"},
            {"list_imports", "{}"},
            {"read_chunk", R"({"import":"i1","chunk":1})"},
            {"save_points", R"({"import":"i1","chunk":1,"points":[]})"},
            {"get_points", R"({"import":"i1"})"},
            {"build_study_plan", R"({"imports":[],"plan":{"title":"T","overview":"O","modules":[]}})"},
            {"create_flashcards", R"({"title":"T","cards":[{"question":"Q","answer":"A"}]})"},
        };
        QStringList names;
        for (const auto &c : calls)
            names.append(c.first);
        QStringList catalogNames;
        for (const AgentTool &t : MCPToolCatalog::tools())
            catalogNames.append(t.name);
        QCOMPARE(names, catalogNames);
        for (const auto &c : calls) {
            const auto viaServer = request(c.first, c.second);
            QVERIFY2(viaServer.has_value(), qPrintable(c.first));
            QVERIFY2(MCPToolCatalog::request(c.first, parseObject(c.second)) == *viaServer, qPrintable(c.first));
        }
    }

    void catalogErrorsAreOneLine()
    {
        auto unknown = caught<ToolCallError>([] { MCPToolCatalog::request("nope", {}); });
        QVERIFY(unknown.has_value());
        QVERIFY(*unknown == ToolCallError::unknownTool("nope"));
        QCOMPARE(unknown->description(), QStringLiteral("unknown tool: nope"));
        auto bad = caught<ToolCallError>([] { MCPToolCatalog::request("read_chunk", parseObject(R"({"import":"i1","chunk":"x"})")); });
        QVERIFY(bad.has_value());
        QCOMPARE(bad->description(), QStringLiteral("Invalid arguments: chunk: expected an integer"));
    }

    void lenientIntegersAndNulls()
    {
        REQ("read_note", R"({"note":"n1","page":"2"})", BridgeRequest::readNote("n1", 2));
        REQ("read_note", R"({"note":"n1","page":2.0})", BridgeRequest::readNote("n1", 2));
        REQ("read_note", R"({"note":"n1","page":null})", BridgeRequest::readNote("n1"));
    }

    // MARK: validation

    void validationErrors()
    {
        QCOMPARE(invalid("read_note", "{}"), QStringLiteral("Invalid arguments: note: required"));
        QCOMPARE(invalid("read_note", R"({"note":5})"), QStringLiteral("Invalid arguments: note: expected a string"));
        QCOMPARE(invalid("read_note", R"({"note":"n1","page":1.5})"), QStringLiteral("Invalid arguments: page: expected an integer"));
        QCOMPARE(invalid("read_note", R"({"note":"n1","page":true})"), QStringLiteral("Invalid arguments: page: expected an integer"));
        QCOMPARE(invalid("write", R"({"note":"n1","page":1,"markdown":"x","mode":"insert"})"),
                 QStringLiteral("Invalid arguments: mode: expected one of append, replace"));
        QCOMPARE(invalid("write", R"({"note":"n1","page":1,"markdown":"x"})"), QStringLiteral("Invalid arguments: mode: required"));
        QCOMPARE(invalid("create_note", R"({"title":"T","md":"x"})"),
                 QStringLiteral("Invalid arguments: md: unknown argument (expected title, markdown, folder)"));
        QCOMPARE(invalid("list_notes", "[1,2]"), QStringLiteral("Invalid arguments: expected an object"));
        QCOMPARE(invalid("save_points", R"({"import":"i1","chunk":1,"points":"x"})"),
                 QStringLiteral("Invalid arguments: points: expected an array"));
        QCOMPARE(invalid("save_points", R"({"import":"i1","chunk":1,"points":[{"text":"a","importance":"high","ref":"r"}]})"),
                 QStringLiteral("Invalid arguments: points[0].importance: expected one of must, good, skip"));
        QCOMPARE(invalid("save_points", R"({"import":"i1","chunk":1,"points":[{"text":"a","importance":"must"}]})"),
                 QStringLiteral("Invalid arguments: points[0].ref: required"));
        QCOMPARE(invalid("build_study_plan", R"({"imports":["i1"],"plan":{"title":"T","overview":"O"}})"),
                 QStringLiteral("Invalid arguments: plan.modules: required"));
        QCOMPARE(invalid("build_study_plan", R"({"imports":[1],"plan":{"title":"T","overview":"O","modules":[]}})"),
                 QStringLiteral("Invalid arguments: imports[0]: expected a string"));
        QCOMPARE(invalid("build_study_plan",
                         R"({"imports":[],"plan":{"title":"T","overview":"O","modules":[{"title":"M","minutes":"soon","points":[]}]}})"),
                 QStringLiteral("Invalid arguments: plan.modules[0].minutes: expected an integer"));
    }
};

QTEST_GUILESS_MAIN(MCPToolsTests)
#include "MCPToolsTests.moc"
