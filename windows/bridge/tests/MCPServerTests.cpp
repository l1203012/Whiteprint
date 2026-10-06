#include "BridgeTestSupport.h"
#include "bridge/MCPServer.h"
#include "core/DSLReference.h"
#include "core/StudyPrompt.h"

using namespace wp;
using namespace wp::testing;

class MCPServerTests : public QObject
{
    Q_OBJECT

    QList<BridgeRequest> sent;
    MCPServer *server = nullptr;

    static MCPServer *makeServer(MCPServer::Send send) { return new MCPServer(std::move(send)); }

    QJsonObject parse(const std::optional<QString> &line)
    {
        if (!line) {
            QTest::qFail("expected a reply", __FILE__, __LINE__);
            return {};
        }
        CHECK2(!line->contains('\n'), "reply must be a single line");
        return parseObject(*line);
    }

    QJsonObject call(const QString &method, const std::optional<QJsonObject> &params = std::nullopt,
                     const QJsonValue &id = 1)
    {
        QJsonObject message{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
        if (params)
            message.insert("params", *params);
        const QString line = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
        return parse(server->handle(line));
    }

    QJsonObject result(const QString &method, const std::optional<QJsonObject> &params = std::nullopt)
    {
        const QJsonObject response = call(method, params);
        CHECK2(!response.contains("error"), qPrintable(method));
        return response.value("result").toObject();
    }

    static int errorCode(const QJsonObject &response)
    {
        return response.value("error").toObject().value("code").toInt();
    }

    QString promptText(const QJsonObject &arguments, const QString &name = "study_plan")
    {
        const QJsonObject r = result("prompts/get", QJsonObject{{"name", name}, {"arguments", arguments}});
        const QJsonArray messages = r.value("messages").toArray();
        if (messages.size() != 1)
            return "<bad>";
        const QJsonObject message = messages.at(0).toObject();
        if (message.value("role").toString() != "user")
            return "<bad>";
        const QJsonObject content = message.value("content").toObject();
        if (content.value("type").toString() != "text")
            return "<bad>";
        return content.value("text").toString();
    }

private slots:
    void init()
    {
        sent.clear();
        delete server;
        server = makeServer([this](const BridgeRequest &r) {
            sent.append(r);
            return BridgeResponse::ok("ok");
        });
    }

    void cleanup()
    {
        delete server;
        server = nullptr;
    }

    void initializeEchoesSupportedVersion()
    {
        for (const QString version : {"2025-06-18", "2025-03-26", "2024-11-05"}) {
            const auto r = result("initialize", QJsonObject{{"protocolVersion", version},
                                                            {"capabilities", QJsonObject()},
                                                            {"clientInfo", QJsonObject{{"name", "t"}, {"version", "1"}}}});
            QCOMPARE(r.value("protocolVersion").toString(), version);
        }
    }

    void initializeOffersNewestForUnknownVersion()
    {
        QCOMPARE(result("initialize", QJsonObject{{"protocolVersion", "2099-01-01"}}).value("protocolVersion").toString(),
                 QStringLiteral("2025-06-18"));
        QCOMPARE(result("initialize").value("protocolVersion").toString(), QStringLiteral("2025-06-18"));
    }

    void initializeDescribesServer()
    {
        const auto r = result("initialize", QJsonObject{{"protocolVersion", "2025-06-18"}});
        QCOMPARE(r.value("capabilities").toObject().keys(), (QStringList{"prompts", "resources", "tools"}));
        const auto info = r.value("serverInfo").toObject();
        QCOMPARE(info.value("name").toString(), QStringLiteral("whiteprint"));
        QVERIFY(info.value("version").isString());
        QVERIFY(r.value("instructions").toString().contains("whiteprint://dsl"));
    }

    void responsesEchoIDs()
    {
        QCOMPARE(call("ping", std::nullopt, 7).value("id").toInt(), 7);
        QCOMPARE(call("ping", std::nullopt, "abc").value("id").toString(), QStringLiteral("abc"));
        QCOMPARE(call("ping").value("jsonrpc").toString(), QStringLiteral("2.0"));
    }

    void notificationsGetNoReply()
    {
        QVERIFY(!server->handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
        QVERIFY(!server->handle(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1}})"));
        QVERIFY(!server->handle(R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"list_notes"}})"));
        QVERIFY(sent.isEmpty());
    }

    void clientResponsesGetNoReply()
    {
        QVERIFY(!server->handle(R"({"jsonrpc":"2.0","id":3,"result":{}})"));
    }

    void ping()
    {
        QVERIFY(result("ping").isEmpty());
    }

    void parseError()
    {
        const auto r = parse(server->handle("{not json"));
        QCOMPARE(errorCode(r), -32700);
        QVERIFY(r.value("id").isNull());
    }

    void invalidRequests()
    {
        for (const QString line : {R"({"id":1,"method":"ping"})", R"({"jsonrpc":"2.0","id":1})",
                                   R"({"jsonrpc":"2.0","id":1,"method":5})", "42", "[]"}) {
            QVERIFY2(errorCode(parse(server->handle(line))) == -32600, qPrintable(line));
        }
    }

    void unknownMethod()
    {
        QCOMPARE(errorCode(call("nope/nope")), -32601);
    }

    void batch()
    {
        const QString line =
            R"([{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/initialized"},{"jsonrpc":"2.0","id":2,"method":"x"}])";
        const auto reply = server->handle(line);
        QVERIFY(reply.has_value());
        const QJsonArray replies = parseValue(*reply).toArray();
        QCOMPARE(replies.size(), 2);
        QCOMPARE(replies.at(0).toObject().value("id").toInt(), 1);
        QCOMPARE(replies.at(1).toObject().value("id").toInt(), 2);
    }

    void resources()
    {
        const auto list = result("resources/list").value("resources").toArray();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).toObject().value("uri").toString(), QStringLiteral("whiteprint://dsl"));
        QCOMPARE(list.at(0).toObject().value("mimeType").toString(), QStringLiteral("text/plain"));

        const auto contents = result("resources/read", QJsonObject{{"uri", "whiteprint://dsl"}}).value("contents").toArray();
        const auto first = contents.at(0).toObject();
        QCOMPARE(first.value("text").toString(), WhiteprintText::dslReference());
        QCOMPARE(first.value("mimeType").toString(), QStringLiteral("text/plain"));
        QCOMPARE(first.value("uri").toString(), QStringLiteral("whiteprint://dsl"));

        QCOMPARE(errorCode(call("resources/read", QJsonObject{{"uri", "whiteprint://nope"}})), -32002);
        QCOMPARE(errorCode(call("resources/read", QJsonObject())), -32602);
        QVERIFY(result("resources/templates/list").value("resourceTemplates").isArray());
    }

    void prompts()
    {
        const auto list = result("prompts/list").value("prompts").toArray();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toObject().value("name").toString(), QStringLiteral("study_plan"));
        QCOMPARE(list.at(1).toObject().value("name").toString(), QStringLiteral("flashcards"));
        const auto imports = list.at(0).toObject().value("arguments").toArray().at(0).toObject();
        QCOMPARE(imports.value("name").toString(), QStringLiteral("imports"));
        QCOMPARE(imports.value("required").toBool(true), false);
        const auto source = list.at(1).toObject().value("arguments").toArray().at(0).toObject();
        QCOMPARE(source.value("name").toString(), QStringLiteral("source"));
        QCOMPARE(source.value("required").toBool(false), true);

        QCOMPARE(promptText({}), WhiteprintText::studyPlanPrompt());
        QCOMPARE(promptText(QJsonObject{{"imports", "i1, i2"}}), WhiteprintText::studyPlanPrompt() + "\n\nImports: i1, i2");
        QCOMPARE(promptText(QJsonObject{{"source", "n3"}}, "flashcards"), WhiteprintText::flashcardsPrompt() + "\n\nSource: n3");
        QCOMPARE(errorCode(call("prompts/get", QJsonObject{{"name", "flashcards"}, {"arguments", QJsonObject()}})), -32602);
        QCOMPARE(errorCode(call("prompts/get", QJsonObject{{"name", "other"}})), -32602);
        QCOMPARE(errorCode(call("prompts/get", QJsonObject())), -32602);
    }

    void promptsAreCompact()
    {
        QVERIFY(WhiteprintText::studyPlanPrompt().toUtf8().size() < 2200);
        QVERIFY(WhiteprintText::flashcardsPrompt().toUtf8().size() < 900);
        for (const QString tool : {"list_imports", "read_chunk", "save_points", "get_points", "build_study_plan"})
            QVERIFY2(WhiteprintText::studyPlanPrompt().contains(tool), qPrintable(tool));
        for (const QString tool : {"read_note", "read_chunk", "get_points", "create_flashcards"})
            QVERIFY2(WhiteprintText::flashcardsPrompt().contains(tool), qPrintable(tool));
    }

    void toolCallReturnsReplyText()
    {
        const auto r = result("tools/call", QJsonObject{{"name", "add_page"}, {"arguments", QJsonObject{{"note", "n1"}}}});
        QCOMPARE(r.value("isError").toBool(true), false);
        const auto content = r.value("content").toArray();
        QCOMPARE(content.size(), 1);
        QCOMPARE(content.at(0).toObject().value("type").toString(), QStringLiteral("text"));
        QCOMPARE(content.at(0).toObject().value("text").toString(), QStringLiteral("ok"));
        QCOMPARE(sent.size(), 1);
        QVERIFY(sent.at(0) == BridgeRequest::addPage("n1"));
    }

    void toolCallWithoutArguments()
    {
        result("tools/call", QJsonObject{{"name", "list_notes"}});
        QCOMPARE(sent.size(), 1);
        QVERIFY(sent.at(0) == BridgeRequest::listNotes());
    }

    void appFailureIsErrorResult()
    {
        delete server;
        server = makeServer([](const BridgeRequest &) { return BridgeResponse::failure("no note 'n9'"); });
        const auto r = result("tools/call", QJsonObject{{"name", "add_page"}, {"arguments", QJsonObject{{"note", "n9"}}}});
        QCOMPARE(r.value("isError").toBool(false), true);
        QCOMPARE(r.value("content").toArray().at(0).toObject().value("text").toString(), QStringLiteral("no note 'n9'"));
    }

    void transportErrorIsErrorResult()
    {
        delete server;
        server = makeServer([](const BridgeRequest &) -> BridgeResponse { throw BridgeError::timedOut(120); });
        const auto r = result("tools/call", QJsonObject{{"name", "list_notes"}, {"arguments", QJsonObject()}});
        QCOMPARE(r.value("isError").toBool(false), true);
        QCOMPARE(r.value("content").toArray().at(0).toObject().value("text").toString(),
                 QStringLiteral("Whiteprint didn't reply within 120 s"));
    }

    void unknownToolIsInvalidParams()
    {
        QCOMPARE(errorCode(call("tools/call", QJsonObject{{"name", "explode"}, {"arguments", QJsonObject()}})), -32602);
        QCOMPARE(errorCode(call("tools/call", QJsonObject{{"arguments", QJsonObject()}})), -32602);
    }

    void outputEscapesNewlinesAndKeepsSlashes()
    {
        delete server;
        server = makeServer([](const BridgeRequest &) { return BridgeResponse::ok("line 1\nline 2 a/b"); });
        const auto line =
            server->handle(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_notes"}})");
        QVERIFY(line.has_value());
        QVERIFY(!line->contains('\n'));
        QVERIFY(line->contains("a/b"));
    }
};

QTEST_GUILESS_MAIN(MCPServerTests)
#include "MCPServerTests.moc"
