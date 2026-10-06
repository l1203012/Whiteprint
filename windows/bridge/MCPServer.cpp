#include "bridge/MCPServer.h"
#include "bridge/MCPTools.h"
#include "core/DSLReference.h"
#include "core/StudyPrompt.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace wp {

namespace {

struct RPCError {
    int code;
    QString message;
};

QJsonObject errorObject(const QJsonValue &id, int code, const QString &message)
{
    return {{"jsonrpc", "2.0"},
            {"id", id.isUndefined() ? QJsonValue(QJsonValue::Null) : id},
            {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject toolResult(const QString &text, bool isError)
{
    return {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}}, {"isError", isError}};
}

QString encode(const QJsonValue &value)
{
    const QJsonDocument doc = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

/// Parses any JSON value, including bare scalars (which QJsonDocument rejects at the top level).
std::optional<QJsonValue> parseMessage(const QString &line)
{
    const QByteArray bytes = line.toUtf8();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error == QJsonParseError::NoError)
        return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    const QJsonDocument wrapped = QJsonDocument::fromJson("[" + bytes + "]", &error);
    if (error.error == QJsonParseError::NoError && wrapped.array().size() == 1)
        return wrapped.array().at(0);
    return std::nullopt;
}

/// An MCP prompt with one optional or required argument, appended to its text as a
/// `Label: value` line.
struct MCPPrompt {
    QString name;
    QString description;
    QString argName;
    QString argLabel;
    QString argDescription;
    bool argRequired;
    QString text;

    QJsonObject json() const
    {
        const QJsonObject argument{{"name", argName}, {"description", argDescription}, {"required", argRequired}};
        return {{"name", name}, {"description", description}, {"arguments", QJsonArray{argument}}};
    }
};

const QList<MCPPrompt> &prompts()
{
    static const QList<MCPPrompt> list = {
        {"study_plan", "Build a study plan from imported course material.", "imports", "Imports",
         "Import ids, comma-separated (default: all)", false, WhiteprintText::studyPlanPrompt()},
        {"flashcards", "Make flashcards from a note or an import.", "source", "Source",
         "A note or import id, e.g. n3 or i2", true, WhiteprintText::flashcardsPrompt()},
    };
    return list;
}

QString version()
{
    const QString v = QCoreApplication::applicationVersion();
    return v.isEmpty() ? QStringLiteral("dev") : v;
}

} // namespace

QString MCPServer::instructions()
{
    return QStringLiteral("Whiteprint is a notes app. Get note ids from list_notes; pages are 1-based. "
                          "Read resource %1 before your first draw.")
        .arg(dslURI());
}

std::optional<QString> MCPServer::handle(const QString &line) const
{
    const auto message = parseMessage(line);
    if (!message)
        return encode(errorObject(QJsonValue::Null, -32700, QStringLiteral("parse error")));
    if (message->isArray()) {
        const QJsonArray batch = message->toArray();
        if (batch.isEmpty())
            return encode(errorObject(QJsonValue::Null, -32600, QStringLiteral("empty batch")));
        QJsonArray replies;
        for (const QJsonValue &item : batch) {
            if (const auto reply = respond(item))
                replies.append(*reply);
        }
        if (replies.isEmpty())
            return std::nullopt;
        return encode(replies);
    }
    if (const auto reply = respond(*message))
        return encode(*reply);
    return std::nullopt;
}

void MCPServer::run() const
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string raw;
    while (std::getline(std::cin, raw)) {
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();
        const QString line = QString::fromUtf8(raw.data(), static_cast<qsizetype>(raw.size()));
        if (line.trimmed().isEmpty())
            continue;
        const auto reply = handle(line);
        if (!reply)
            continue;
        const QByteArray out = reply->toUtf8() + '\n';
        std::fwrite(out.constData(), 1, static_cast<size_t>(out.size()), stdout);
        std::fflush(stdout);
    }
}

std::optional<QJsonObject> MCPServer::respond(const QJsonValue &message) const
{
    if (!message.isObject())
        return errorObject(QJsonValue::Null, -32600, QStringLiteral("invalid request"));
    const QJsonObject object = message.toObject();
    const QJsonValue id = object.value("id");
    const QJsonValue method = object.value("method");
    if (object.value("jsonrpc").toString() != QLatin1String("2.0") || !method.isString()) {
        // A response from the client (we never send requests) needs no reply.
        if (!object.contains("method") && (object.contains("result") || object.contains("error")))
            return std::nullopt;
        return errorObject(id, -32600, QStringLiteral("invalid request"));
    }
    if (!id.isString() && !id.isDouble()) {
        return id.isUndefined() ? std::nullopt
                                : std::optional<QJsonObject>(errorObject(QJsonValue::Null, -32600, QStringLiteral("invalid id")));
    }
    const QJsonObject params = object.value("params").toObject();
    try {
        return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result(method.toString(), params)}};
    } catch (const RPCError &e) {
        return errorObject(id, e.code, e.message);
    } catch (const std::exception &e) {
        return errorObject(id, -32603, QString::fromUtf8(e.what()));
    }
}

QJsonObject MCPServer::result(const QString &method, const QJsonObject &params) const
{
    if (method == QLatin1String("initialize")) {
        const QString requested = params.value("protocolVersion").toString();
        const QStringList versions = protocolVersions();
        return {{"protocolVersion", versions.contains(requested) ? requested : versions.first()},
                {"capabilities", QJsonObject{{"tools", QJsonObject()}, {"resources", QJsonObject()},
                                             {"prompts", QJsonObject()}}},
                {"serverInfo", QJsonObject{{"name", "whiteprint"}, {"version", version()}}},
                {"instructions", instructions()}};
    }
    if (method == QLatin1String("ping"))
        return {};
    if (method == QLatin1String("tools/list")) {
        QJsonArray tools;
        for (const AgentTool &tool : MCPToolCatalog::tools()) {
            QJsonObject annotations;
            for (auto it = tool.annotations.begin(); it != tool.annotations.end(); ++it)
                annotations.insert(it.key(), it.value());
            tools.append(QJsonObject{{"name", tool.name},
                                     {"description", tool.description},
                                     {"inputSchema", tool.inputSchema},
                                     {"annotations", annotations}});
        }
        return {{"tools", tools}};
    }
    if (method == QLatin1String("tools/call"))
        return callTool(params);
    if (method == QLatin1String("resources/list")) {
        return {{"resources", QJsonArray{QJsonObject{{"uri", dslURI()},
                                                     {"name", "dsl"},
                                                     {"description", "Drawing language reference"},
                                                     {"mimeType", "text/plain"}}}}};
    }
    if (method == QLatin1String("resources/templates/list"))
        return {{"resourceTemplates", QJsonArray()}};
    if (method == QLatin1String("resources/read")) {
        if (!params.value("uri").isString())
            throw RPCError{-32602, QStringLiteral("missing uri")};
        const QString uri = params.value("uri").toString();
        if (uri != dslURI())
            throw RPCError{-32002, QStringLiteral("resource not found: %1").arg(uri)};
        return {{"contents", QJsonArray{QJsonObject{{"uri", uri},
                                                    {"mimeType", "text/plain"},
                                                    {"text", WhiteprintText::dslReference()}}}}};
    }
    if (method == QLatin1String("prompts/list")) {
        QJsonArray list;
        for (const MCPPrompt &p : prompts())
            list.append(p.json());
        return {{"prompts", list}};
    }
    if (method == QLatin1String("prompts/get"))
        return prompt(params);
    throw RPCError{-32601, QStringLiteral("method not found: %1").arg(method)};
}

QJsonObject MCPServer::callTool(const QJsonObject &params) const
{
    if (!params.value("name").isString())
        throw RPCError{-32602, QStringLiteral("missing tool name")};
    const QString name = params.value("name").toString();
    if (!MCPTool::named(name))
        throw RPCError{-32602, ToolCallError::unknownTool(name).description()};

    BridgeRequest request;
    try {
        const QJsonValue arguments = params.contains("arguments") ? params.value("arguments") : QJsonValue(QJsonObject());
        if (!arguments.isObject())
            throw ToolCallError::invalidArguments(QStringLiteral("expected an object"));
        request = MCPToolCatalog::request(name, arguments.toObject());
    } catch (const ToolCallError &e) {
        return toolResult(e.description(), true);
    }
    try {
        const BridgeResponse response = send_(request);
        return toolResult(response.text, !response.isOk);
    } catch (const BridgeError &e) {
        log(QStringLiteral("%1: %2").arg(name, e.description()));
        return toolResult(e.description(), true);
    } catch (const std::exception &e) {
        log(QStringLiteral("%1: %2").arg(name, QString::fromUtf8(e.what())));
        return toolResult(QString::fromUtf8(e.what()), true);
    }
}

QJsonObject MCPServer::prompt(const QJsonObject &params) const
{
    if (!params.value("name").isString())
        throw RPCError{-32602, QStringLiteral("missing prompt name")};
    const QString name = params.value("name").toString();
    const MCPPrompt *found = nullptr;
    for (const MCPPrompt &p : prompts()) {
        if (p.name == name)
            found = &p;
    }
    if (!found)
        throw RPCError{-32602, QStringLiteral("unknown prompt: %1").arg(name)};
    const QJsonObject arguments = params.value("arguments").toObject();
    const QString value = arguments.value(found->argName).toString().trimmed();
    if (value.isEmpty() && found->argRequired)
        throw RPCError{-32602, QStringLiteral("missing argument: %1").arg(found->argName)};
    const QString text = value.isEmpty() ? found->text
                                         : found->text + QStringLiteral("\n\n%1: %2").arg(found->argLabel, value);
    return {{"description", found->description},
            {"messages", QJsonArray{QJsonObject{{"role", "user"},
                                                {"content", QJsonObject{{"type", "text"}, {"text", text}}}}}}};
}

void MCPServer::log(const QString &message)
{
    const QByteArray out = "whiteprint-mcp: " + message.toUtf8() + '\n';
    std::fwrite(out.constData(), 1, static_cast<size_t>(out.size()), stderr);
    std::fflush(stderr);
}

} // namespace wp
