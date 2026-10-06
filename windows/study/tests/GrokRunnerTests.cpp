#include "core/StudyPrompt.h"
#include "core/tests/TestSupport.h"
#include "study/GrokRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <memory>
#include <thread>

using namespace wp;
using namespace wp::testing;
using Event = RunnerEvent;

namespace {

/// A scripted stand-in for the Grok API on 127.0.0.1: answers each request from the next scripted reply.
class FakeGrok : public QObject
{
public:
    struct Reply {
        enum Kind { json, raw, failure, hang } kind = json;
        int status = 200;
        QByteArray body;
        QMap<QByteArray, QByteArray> headers;
    };
    struct Request {
        QByteArray method, path, authorization;
        QJsonObject body;
    };

    explicit FakeGrok(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &FakeGrok::accept);
        if (!m_server.listen(QHostAddress::LocalHost, 0))
            qFatal("can't listen");
    }

    QUrl baseURL() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort())); }
    void reset(const QList<Reply> &script)
    {
        m_replies = script;
        requests.clear();
        stops = 0;
    }

    static Reply json(int status, const QJsonValue &value, const QMap<QByteArray, QByteArray> &headers = {})
    {
        QJsonDocument doc = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
        return {Reply::json, status, doc.toJson(QJsonDocument::Compact), headers};
    }
    static Reply raw(int status, const QByteArray &text) { return {Reply::raw, status, text, {}}; }
    static Reply failure() { return {Reply::failure, 0, {}, {}}; }
    static Reply hang() { return {Reply::hang, 0, {}, {}}; }

    QList<Request> requests;
    /// Connections the client dropped without an answer (cancelled requests).
    int stops = 0;

private:
    void accept()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            auto answered = std::make_shared<bool>(false);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, answered] {
                buffer->append(socket->readAll());
                if (!*answered && complete(*buffer)) {
                    *answered = true;
                    respond(socket, *buffer);
                }
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket, answered] {
                if (!*answered || m_hung.contains(socket))
                    ++stops;
                m_hung.remove(socket);
                socket->deleteLater();
            });
        }
    }

    static bool complete(const QByteArray &data)
    {
        const qsizetype end = data.indexOf("\r\n\r\n");
        if (end < 0)
            return false;
        qsizetype length = 0;
        for (const QByteArray &line : data.left(end).split('\n')) {
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toLongLong();
        }
        return data.size() >= end + 4 + length;
    }

    void respond(QTcpSocket *socket, const QByteArray &data)
    {
        const qsizetype end = data.indexOf("\r\n\r\n");
        const QList<QByteArray> lines = data.left(end).split('\n');
        const QList<QByteArray> start = lines[0].trimmed().split(' ');
        Request request;
        request.method = start.value(0);
        request.path = start.value(1);
        for (const QByteArray &line : lines.mid(1)) {
            if (line.toLower().startsWith("authorization:"))
                request.authorization = line.mid(14).trimmed();
        }
        request.body = QJsonDocument::fromJson(data.mid(end + 4)).object();
        requests.append(request);

        // A failure is sticky: Qt retries a dropped connection, and the retry must fail too.
        const bool sticky = !m_replies.isEmpty() && m_replies.first().kind == Reply::failure;
        const Reply reply = m_replies.isEmpty() ? raw(500, "script ran out") : sticky ? m_replies.first() : m_replies.takeFirst();
        switch (reply.kind) {
        case Reply::hang:
            m_hung.insert(socket);
            return;
        case Reply::failure:
            socket->abort();
            return;
        default:
            break;
        }
        QByteArray out = "HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
            + QByteArray::number(reply.body.size()) + "\r\nConnection: close\r\n";
        for (auto it = reply.headers.begin(); it != reply.headers.end(); ++it)
            out += it.key() + ": " + it.value() + "\r\n";
        out += "\r\n" + reply.body;
        socket->write(out);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QList<Reply> m_replies;
    QSet<QTcpSocket *> m_hung;
};

} // namespace

class GrokRunnerTests : public QObject
{
    Q_OBJECT

    std::unique_ptr<FakeGrok> grok;
    QList<Event> events;
    struct Executed {
        QString name;
        QJsonObject arguments;
    };
    QList<Executed> executed;
    QList<RunnerTool> tools = {{"list_imports", "List imports.", QJsonObject{{"type", "object"}}}};

    GrokRunner::Configuration configuration() const
    {
        return {"xai-secret", GrokRunner::Configuration::defaultModel(), grok->baseURL()};
    }

    /// A runner whose tool calls succeed with `result(name)` (or fail when it starts with `!`).
    std::unique_ptr<GrokRunner> runner(std::function<QString(const QString &)> result = [](const QString &n) { return "ok " + n; })
    {
        auto r = std::make_unique<GrokRunner>(
            configuration(), tools, [this, result](const QString &name, const QJsonObject &arguments, GrokRunner::Reply reply) {
                QCOMPARE(QThread::currentThread(), thread());
                executed.append({name, arguments});
                const QString text = result(name);
                std::thread([reply, text] { reply(text.startsWith('!') ? text.mid(1) : text, text.startsWith('!')); }).detach();
            });
        r->backoff = 0.01;
        return r;
    }

    void start(GrokRunner &runner, const QStringList &imports = {"i1"})
    {
        runner.start(imports, [this](const Event &e) {
            QCOMPARE(QThread::currentThread(), thread());
            events.append(e);
        });
    }

    void waitForEnd()
    {
        QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty() && events.last().isTerminal(), 15000);
        // Anything delivered after the end would be a bug.
        QTest::qWait(100);
    }

    static FakeGrok::Reply toolCalls(const QList<std::tuple<QString, QString, QString>> &calls, const QJsonValue &content = QJsonValue::Null)
    {
        QJsonArray array;
        for (const auto &[id, name, arguments] : calls)
            array.append(QJsonObject{{"id", id}, {"type", "function"}, {"function", QJsonObject{{"name", name}, {"arguments", arguments}}}});
        const QJsonObject message{{"role", "assistant"}, {"content", content}, {"tool_calls", array}};
        return FakeGrok::json(200, QJsonObject{{"choices", QJsonArray{QJsonObject{{"index", 0}, {"message", message}, {"finish_reason", "tool_calls"}}}}});
    }
    static FakeGrok::Reply answer(const QString &text)
    {
        const QJsonObject message{{"role", "assistant"}, {"content", text}};
        return FakeGrok::json(200, QJsonObject{{"choices", QJsonArray{QJsonObject{{"index", 0}, {"message", message}, {"finish_reason", "stop"}}}}});
    }

    static QStringList roles(const QJsonArray &messages)
    {
        QStringList r;
        for (const auto &m : messages)
            r << m.toObject()["role"].toString();
        return r;
    }
    static QStringList toolResults(const QJsonArray &messages)
    {
        QStringList r;
        for (const auto &m : messages)
            if (m.toObject()["role"] == "tool")
                r << m.toObject()["content"].toString();
        return r;
    }

private slots:
    void init()
    {
        grok = std::make_unique<FakeGrok>();
        events.clear();
        executed.clear();
    }
    void cleanup() { grok.reset(); }

    // MARK: tool loop

    void runsToolCallsUntilGrokAnswers()
    {
        grok->reset({
            toolCalls({{"call_1", "list_imports", "{}"}}),
            toolCalls({{"call_2", "read_chunk", R"({"import":"i1","chunk":1})"},
                       {"call_3", "save_points", R"({"import":"i1","chunk":1,"points":[]})"}},
                      "Reading."),
            answer("Done."),
        });
        auto r = runner([](const QString &n) { return n == "read_chunk" ? QString("chunk text ").repeated(500) : QString("ok"); });
        start(*r, {"i1", "i2"});
        QVERIFY(r->isRunning());
        waitForEnd();

        const QList<Event> expected = {
            Event::status("Starting Grok…"), Event::status("Looking at your imports…"), Event::text("Reading."),
            Event::status("Reading chunk 1 · i1"), Event::status("Saving points…"), Event::text("Done."), Event::finished(),
        };
        WP_EQ(events, expected);
        QVERIFY(!r->isRunning());
        QCOMPARE(executed.size(), qsizetype(3));
        QCOMPARE(executed[0].name, QString("list_imports"));
        QCOMPARE(executed[1].name, QString("read_chunk"));
        QCOMPARE(executed[2].name, QString("save_points"));
        QCOMPARE(executed[1].arguments["chunk"].toInt(), 1);

        QCOMPARE(grok->requests.size(), qsizetype(3));
        for (const auto &request : grok->requests) {
            QCOMPARE(request.path, QByteArray("/v1/chat/completions"));
            QCOMPARE(request.method, QByteArray("POST"));
            QCOMPARE(request.authorization, QByteArray("Bearer xai-secret"));
        }
        QCOMPARE(grok->requests[0].body["model"].toString(), QString("grok-4.7"));
        const QJsonObject tool = grok->requests[0].body["tools"].toArray()[0].toObject();
        QCOMPARE(tool["type"].toString(), QString("function"));
        const QJsonObject function = tool["function"].toObject();
        QCOMPARE(function["name"].toString(), QString("list_imports"));
        QCOMPARE(function["description"].toString(), QString("List imports."));
        WP_EQ(function["parameters"].toObject(), (QJsonObject{{"type", "object"}}));

        const QJsonArray first = grok->requests[0].body["messages"].toArray();
        QCOMPARE(roles(first), QStringList({"system", "user"}));
        QCOMPARE(first[0].toObject()["content"].toString(), WhiteprintText::studyPlanPrompt() + "\n\nImports: i1, i2");

        const QJsonArray last = grok->requests[2].body["messages"].toArray();
        QCOMPARE(roles(last), QStringList({"system", "user", "assistant", "tool", "assistant", "tool", "tool"}));
        QCOMPARE(last[3].toObject()["tool_call_id"].toString(), QString("call_1"));
        QCOMPARE(last[3].toObject()["content"].toString(), QString("ok"));
        QCOMPARE(last[4].toObject()["content"].toString(), QString("Reading."));
        QCOMPARE(last[4].toObject()["tool_calls"].toArray().size(), 2);
        QCOMPARE(last[5].toObject()["tool_call_id"].toString(), QString("call_2"));
        QCOMPARE(last[5].toObject()["content"].toString(), QString("[chunk 1 of i1: read, points saved]"));
        QCOMPARE(last[6].toObject()["tool_call_id"].toString(), QString("call_3"));
    }

    void chunkTextStaysUntilItsPointsAreSaved()
    {
        grok->reset({
            toolCalls({{"a", "read_chunk", R"({"import":"i1","chunk":1})"}}),
            toolCalls({{"b", "read_chunk", R"({"import":"i1","chunk":2})"}}),
            toolCalls({{"c", "save_points", R"({"import":"i1","chunk":"2","points":[]})"}}),
            answer(""),
        });
        auto r = runner([](const QString &n) { return n == "read_chunk" ? QString("TEXT") : QString("ok"); });
        start(*r);
        waitForEnd();
        QCOMPARE(events.last(), Event::finished());
        const QStringList results = toolResults(grok->requests.last().body["messages"].toArray());
        QCOMPARE(results, QStringList({"TEXT", "[chunk 2 of i1: read, points saved]", "ok"}));
        QVERIFY(!events.contains(Event::text("")));
    }

    void toolErrorsAndBadArgumentsGoBackToGrok()
    {
        grok->reset({
            toolCalls({{"a", "save_points", R"({"import":"i1","chunk":1})"}, {"b", "list_imports", "not json"}}),
            answer("Sorry."),
        });
        auto r = runner([](const QString &) { return QString("!points: required"); });
        start(*r);
        waitForEnd();
        QCOMPARE(events.last(), Event::finished());
        QCOMPARE(executed.size(), qsizetype(1));
        QCOMPARE(executed[0].name, QString("save_points"));
        const QJsonArray messages = grok->requests.last().body["messages"].toArray();
        QCOMPARE(toolResults(messages), QStringList({"Error: points: required", "Error: Invalid arguments: expected a JSON object"}));
        QStringList ids;
        for (const auto &m : messages)
            if (m.toObject()["role"] == "tool")
                ids << m.toObject()["tool_call_id"].toString();
        QCOMPARE(ids, QStringList({"a", "b"}));
    }

    void stopsAfterTooManyToolCalls()
    {
        QList<FakeGrok::Reply> script;
        for (int i = 0; i < 10; ++i)
            script << toolCalls({{"x", "list_imports", "{}"}});
        grok->reset(script);
        auto r = runner();
        r->maxToolCalls = 3;
        start(*r);
        waitForEnd();
        QCOMPARE(events.last(), Event::failed("Grok stopped after 3 tool calls without finishing the study plan."));
        QCOMPARE(executed.size(), qsizetype(3));
        QCOMPARE(grok->requests.size(), qsizetype(4));
    }

    void refusesASecondRun()
    {
        grok->reset({FakeGrok::hang()});
        auto r = runner();
        start(*r);
        const auto error = caught<StudyError>([&] { start(*r); });
        QVERIFY(error && *error == StudyError::alreadyRunning());
        r->cancel();
    }

    // MARK: errors

    void rejectedKey()
    {
        for (int status : {401, 403}) {
            events.clear();
            grok->reset({FakeGrok::json(status, QJsonObject{{"error", "Incorrect API key provided: xa***et."}})});
            auto r = runner();
            start(*r);
            waitForEnd();
            WP_EQ(events, (QList<Event>{Event::status("Starting Grok…"), Event::failed("Grok rejected the API key. Check it in Settings.")}));
            QCOMPARE(grok->requests.size(), qsizetype(1));
        }
    }

    void retriesWhenBusyHonouringRetryAfter()
    {
        grok->reset({
            FakeGrok::json(429, QJsonObject{{"error", "slow down"}}, {{"Retry-After", "0"}}),
            FakeGrok::raw(503, "upstream"),
            answer("Done."),
        });
        auto r = runner();
        start(*r);
        waitForEnd();
        const QList<Event> expected = {Event::status("Starting Grok…"), Event::status("Grok is busy, retrying…"),
                                       Event::status("Grok had a problem, retrying…"), Event::text("Done."), Event::finished()};
        WP_EQ(events, expected);
        QCOMPARE(grok->requests.size(), qsizetype(3));
    }

    void givesUpAfterThreeTries()
    {
        grok->reset({FakeGrok::raw(500, ""), FakeGrok::raw(502, ""),
                     FakeGrok::json(500, QJsonObject{{"error", QJsonObject{{"message", "model overloaded\nplease wait"}}}})});
        {
            auto r = runner();
            start(*r);
            waitForEnd();
            QCOMPARE(events.last(), Event::failed("Grok had a server problem (HTTP 500): model overloaded please wait"));
            QCOMPARE(grok->requests.size(), qsizetype(3));
        }
        events.clear();
        grok->reset({FakeGrok::json(429, QJsonObject(), {{"Retry-After", "0"}}), FakeGrok::json(429, QJsonObject(), {{"Retry-After", "0"}}),
                     FakeGrok::json(429, QJsonObject(), {{"Retry-After", "0"}})});
        auto r = runner();
        start(*r);
        waitForEnd();
        QCOMPARE(events.last(), Event::failed("Grok is busy or your rate limit is reached. Try again in a few minutes."));
        QCOMPARE(grok->requests.size(), qsizetype(3));
    }

    void otherFailures()
    {
        const QList<std::pair<FakeGrok::Reply, QString>> cases = {
            {FakeGrok::raw(200, "<html>"), "Grok sent an invalid response."},
            {FakeGrok::json(200, QJsonObject{{"choices", QJsonArray()}}), "Grok sent an invalid response."},
            {FakeGrok::json(400, QJsonObject{{"error", "Model not found: grok-9"}}), "Grok returned HTTP 400: Model not found: grok-9"},
        };
        for (const auto &[reply, expected] : cases) {
            events.clear();
            grok->reset({reply});
            auto r = runner();
            start(*r);
            waitForEnd();
            QCOMPARE(events.last(), Event::failed(expected));
            QCOMPARE(grok->requests.size(), qsizetype(1));
        }
        events.clear();
        grok->reset({FakeGrok::failure()});
        auto r = runner();
        start(*r);
        waitForEnd();
        QCOMPARE(events.last().kind, Event::Kind::failed);
        QVERIFY2(events.last().message.startsWith("Couldn't reach Grok: "), qPrintable(events.last().message));
    }

    // MARK: cancellation

    void cancelStopsTheRequestInFlight()
    {
        grok->reset({FakeGrok::hang()});
        auto r = runner();
        start(*r);
        QTRY_COMPARE(grok->requests.size(), qsizetype(1));
        r->cancel();
        waitForEnd();
        WP_EQ(events, (QList<Event>{Event::status("Starting Grok…"), Event::failed("Cancelled")}));
        QVERIFY(!r->isRunning());
        QTRY_COMPARE(grok->stops, 1);
    }

    void cancelDuringAToolCallSendsNothingMore()
    {
        grok->reset({toolCalls({{"a", "list_imports", "{}"}, {"b", "list_imports", "{}"}}), answer("Done.")});
        GrokRunner::Reply pending;
        GrokRunner r(configuration(), tools, [&](const QString &, const QJsonObject &, GrokRunner::Reply reply) { pending = reply; });
        start(r);
        QTRY_VERIFY(pending);
        r.cancel();
        QTest::qWait(50);
        pending("ok", false);
        waitForEnd();
        QCOMPARE(events.last(), Event::failed("Cancelled"));
        QCOMPARE(grok->requests.size(), qsizetype(1));
        QVERIFY(!r.isRunning());
    }

    // MARK: testConnection

    GrokRunner::ConnectionResult connect(const FakeGrok::Reply &reply)
    {
        grok->reset({reply});
        std::optional<GrokRunner::ConnectionResult> result;
        GrokRunner::testConnection(configuration(), [&](const GrokRunner::ConnectionResult &r) { result = r; });
        [&] { QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 10000); }();
        return result.value_or(GrokRunner::ConnectionResult{});
    }

    void connectionLooksTheModelUp()
    {
        const auto result = connect(FakeGrok::json(200, QJsonObject{{"id", "grok-4.7"}, {"object", "model"}, {"owned_by", "xai"}}));
        QVERIFY(result.ok);
        QCOMPARE(result.text, QString("grok-4.7"));
        QCOMPARE(grok->requests.size(), qsizetype(1));
        QCOMPARE(grok->requests[0].path, QByteArray("/v1/models/grok-4.7"));
        QCOMPARE(grok->requests[0].method, QByteArray("GET"));
        QCOMPARE(grok->requests[0].authorization, QByteArray("Bearer xai-secret"));
    }

    void connectionFailures()
    {
        auto result = connect(FakeGrok::json(401, QJsonObject{{"error", "bad key"}}));
        QVERIFY(!result.ok);
        QCOMPARE(result.text, QString("Grok rejected the API key. Check it in Settings."));
        result = connect(FakeGrok::json(404, QJsonObject()));
        QVERIFY(!result.ok);
        QCOMPARE(result.text, QString::fromUtf8("Grok doesn't know the model \xe2\x80\x9cgrok-4.7\xe2\x80\x9d."));
        result = connect(FakeGrok::failure());
        QVERIFY(!result.ok);
        QVERIFY(result.text.startsWith("Couldn't reach Grok: "));
    }
};

QTEST_MAIN(GrokRunnerTests)
#include "GrokRunnerTests.moc"
