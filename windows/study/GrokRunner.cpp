#include "study/GrokRunner.h"

#include "core/StudyPrompt.h"
#include "core/TextUtil.h"
#include "study/ClaudeCodeRunner.h"
#include "study/ToolStatus.h"

#include <QHash>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace wp {

namespace {

QUrl joined(const QUrl &base, const QString &component)
{
    QUrl url = base;
    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    url.setPath(path + component);
    return url;
}

QNetworkRequest makeRequest(const QUrl &url, const QString &apiKey, int timeoutMs)
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
    request.setTransferTimeout(timeoutMs);
    return request;
}

std::optional<int> intValue(const QJsonValue &value)
{
    if (value.isDouble()) {
        const double d = value.toDouble();
        return d == std::floor(d) ? std::optional<int>(int(d)) : std::nullopt;
    }
    if (value.isString()) {
        const auto n = swiftInt(value.toString());
        return n ? std::optional<int>(int(*n)) : std::nullopt;
    }
    return std::nullopt;
}

/// A call's arguments: a JSON string per the API, tolerating an object or nothing.
std::optional<QJsonObject> callArguments(const QJsonValue &value)
{
    if (value.isObject())
        return value.toObject();
    const QString text = value.toString().trimmed();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.isEmpty() ? QByteArray("{}") : text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    return doc.object();
}

} // namespace

// MARK: - Replies

/// An HTTP exchange with Grok, classified.
struct GrokRunner::HttpReply {
    enum class Kind { ok, status, network, invalid };
    Kind kind = Kind::invalid;
    QJsonObject object;
    int code = 0;
    std::optional<double> retryAfter;
    QString detail;
    QString reason;

    static HttpReply from(QNetworkReply *reply)
    {
        HttpReply r;
        const QByteArray data = reply->readAll();
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (!status.isValid()) {
            if (reply->error() != QNetworkReply::NoError) {
                r.kind = Kind::network;
                r.reason = reply->errorString();
            }
            return r;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        r.code = status.toInt();
        if (r.code < 200 || r.code >= 300) {
            r.kind = Kind::status;
            bool ok = false;
            const double seconds = QString::fromLatin1(reply->rawHeader("Retry-After")).trimmed().toDouble(&ok);
            if (ok)
                r.retryAfter = std::max(seconds, 0.0);
            r.detail = detailOf(doc.isObject() ? doc.object() : QJsonObject());
            return r;
        }
        if (doc.isObject()) {
            r.kind = Kind::ok;
            r.object = doc.object();
        }
        return r;
    }

    /// `{"error": "…"}`, `{"error": {"message": "…"}}` or `{"message": "…"}`, cut to one line.
    static QString detailOf(const QJsonObject &object)
    {
        const QJsonValue error = object.value("error");
        QString text;
        if (error.isString())
            text = error.toString();
        else if (error.isObject() && error.toObject().value("message").isString())
            text = error.toObject().value("message").toString();
        else
            text = object.value("message").toString();
        const QString line = oneLine(text);
        if (line.isEmpty())
            return {};
        return line.size() > 200 ? line.left(200) + QStringLiteral("…") : line;
    }

    /// The failure in one line for the user; empty for `ok`.
    QString message() const
    {
        switch (kind) {
        case Kind::ok:
            return {};
        case Kind::status:
            if (code == 401 || code == 403)
                return QStringLiteral("Grok rejected the API key. Check it in Settings.");
            if (code == 429)
                return QStringLiteral("Grok is busy or your rate limit is reached. Try again in a few minutes.");
            if (code >= 500)
                return QStringLiteral("Grok had a server problem (HTTP %1)").arg(code)
                    + (detail.isEmpty() ? QStringLiteral(". Try again later.") : QStringLiteral(": ") + detail);
            return QStringLiteral("Grok returned HTTP %1").arg(code) + (detail.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + detail);
        case Kind::network:
            return QStringLiteral("Couldn't reach Grok: %1").arg(reason);
        case Kind::invalid:
            return QStringLiteral("Grok sent an invalid response.");
        }
        return {};
    }
};

/// One run's conversation and state.
struct GrokRunner::Run {
    EventHandler onEvent;
    QJsonArray messages;
    int toolCalls = 0;
    /// Index in `messages` of each chunk's `read_chunk` result, by `import#chunk`.
    QHash<QString, int> chunkReads;
    QPointer<QNetworkReply> reply;
    bool ended = false;

    void deliver(const Event &event)
    {
        if (ended)
            return;
        ended = event.isTerminal();
        onEvent(event);
    }
};

// MARK: - Running

GrokRunner::GrokRunner(Configuration configuration, QList<RunnerTool> tools, ToolExecutor execute, StudyStore *store,
                       QObject *parent)
    : QObject(parent), m_configuration(std::move(configuration)), m_execute(std::move(execute)), m_store(store),
      m_network(new QNetworkAccessManager(this))
{
    for (const RunnerTool &tool : tools) {
        const QJsonObject function{{"name", tool.name}, {"description", tool.description}, {"parameters", tool.inputSchema}};
        m_tools.append(QJsonObject{{"type", "function"}, {"function", function}});
    }
}

GrokRunner::~GrokRunner()
{
    if (m_run) {
        m_run->ended = true;
        if (m_run->reply)
            m_run->reply->abort();
    }
}

void GrokRunner::start(const QStringList &importIDs, EventHandler onEvent)
{
    if (m_run)
        throw StudyError::alreadyRunning();
    auto run = std::make_shared<Run>();
    run->onEvent = std::move(onEvent);
    m_run = run;
    run->messages = QJsonArray{
        QJsonObject{{"role", "system"}, {"content", ClaudeCodeRunner::prompt(importIDs)}},
        QJsonObject{{"role", "user"}, {"content", "Make the study plan."}},
    };
    run->deliver(Event::status(QStringLiteral("Starting Grok…")));
    send(run);
}

void GrokRunner::cancel()
{
    const auto run = m_run;
    if (!run)
        return;
    const QPointer<QNetworkReply> reply = run->reply;
    end(run, Event::failed(QStringLiteral("Cancelled")));
    if (reply)
        reply->abort();
}

void GrokRunner::end(const std::shared_ptr<Run> &run, const Event &event)
{
    if (m_run == run)
        m_run.reset();
    run->deliver(event);
}

void GrokRunner::send(const std::shared_ptr<Run> &run, int attempt)
{
    if (run->ended)
        return;
    const QJsonObject body{{"model", m_configuration.model},
                           {"messages", run->messages},
                           {"tools", m_tools},
                           {"tool_choice", "auto"}};
    QNetworkRequest request = makeRequest(joined(m_configuration.baseURL, QStringLiteral("chat/completions")), m_configuration.apiKey, 600'000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    run->reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, run, attempt] {
        reply->deleteLater();
        if (run->ended)
            return;
        received(HttpReply::from(reply), run, attempt);
    });
}

void GrokRunner::received(const HttpReply &reply, const std::shared_ptr<Run> &run, int attempt)
{
    if (run->ended)
        return;
    run->reply = nullptr;
    if (reply.kind == HttpReply::Kind::ok) {
        const QJsonArray choices = reply.object.value("choices").toArray();
        const QJsonValue message = choices.isEmpty() ? QJsonValue() : choices.first().toObject().value("message");
        if (!message.isObject())
            return end(run, Event::failed(QStringLiteral("Grok sent an invalid response.")));
        answered(message.toObject(), run);
    } else if (reply.kind == HttpReply::Kind::status && (reply.code == 429 || reply.code >= 500) && attempt < maxAttempts) {
        const double delay = reply.retryAfter ? std::min(*reply.retryAfter, 60.0) : backoff * std::pow(2.0, attempt - 1);
        run->deliver(Event::status(reply.code == 429 ? QStringLiteral("Grok is busy, retrying…")
                                                      : QStringLiteral("Grok had a problem, retrying…")));
        QTimer::singleShot(int(delay * 1000), this, [this, run, attempt] { send(run, attempt + 1); });
    } else {
        end(run, Event::failed(reply.message()));
    }
}

void GrokRunner::answered(const QJsonObject &message, const std::shared_ptr<Run> &run)
{
    const QString text = message.value("content").toString().trimmed();
    if (!text.isEmpty())
        run->deliver(Event::text(text));
    const QJsonArray calls = message.value("tool_calls").toArray();
    if (calls.isEmpty())
        return end(run, Event::finished());
    run->messages.append(QJsonObject{{"role", "assistant"},
                                     {"tool_calls", calls},
                                     {"content", text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text)}});
    runTools(calls, 0, run);
}

/// Runs `calls` from index `from` one after another, then asks Grok to continue.
void GrokRunner::runTools(const QJsonArray &calls, int from, const std::shared_ptr<Run> &run)
{
    if (run->ended)
        return;
    if (from >= calls.size())
        return send(run);
    run->toolCalls += 1;
    if (run->toolCalls > maxToolCalls) {
        return end(run, Event::failed(QStringLiteral("Grok stopped after %1 tool calls without finishing the study plan.")
                                          .arg(maxToolCalls)));
    }
    const QJsonObject call = calls[from].toObject();
    const QString id = call.value("id").toString();
    const QJsonObject function = call.value("function").toObject();
    const QString name = function.value("name").toString();
    const auto arguments = callArguments(function.value("arguments"));
    if (!arguments) {
        record(QStringLiteral("Invalid arguments: expected a JSON object"), true, id, run);
        return runTools(calls, from + 1, run);
    }
    StudyStore *store = m_store;
    run->deliver(Event::status(ToolStatus::text(
        name, *arguments, store ? ImportInfo([store](const QString &i) { return store->importInfo(i); }) : ImportInfo())));
    if (run->ended)
        return;

    const auto replied = std::make_shared<bool>(false);
    const QPointer<GrokRunner> self(this);
    Reply reply = [self, run, replied, calls, from, id, name, args = *arguments](const QString &text, bool isError) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, run, replied, calls, from, id, name, args, text, isError] {
            if (*replied)
                return;
            *replied = true;
            if (run->ended)
                return;
            self->record(text, isError, id, run);
            if (!isError)
                self->trimHistory(name, args, run);
            self->runTools(calls, from + 1, run);
        }, Qt::QueuedConnection);
    };
    m_execute(name, *arguments, std::move(reply));
}

void GrokRunner::record(const QString &text, bool isError, const QString &id, const std::shared_ptr<Run> &run)
{
    run->messages.append(QJsonObject{{"role", "tool"},
                                     {"tool_call_id", id},
                                     {"content", isError ? QStringLiteral("Error: ") + text : text}});
}

/// Remembers where a chunk was read, and once its points are saved replaces that (long) result with a
/// placeholder: Grok never needs it again.
void GrokRunner::trimHistory(const QString &tool, const QJsonObject &arguments, const std::shared_ptr<Run> &run)
{
    if (!arguments.value("import").isString())
        return;
    const QString importID = arguments.value("import").toString();
    const auto chunk = intValue(arguments.value("chunk"));
    if (!chunk)
        return;
    const QString key = QStringLiteral("%1#%2").arg(importID).arg(*chunk);
    if (tool == "read_chunk") {
        run->chunkReads[key] = int(run->messages.size()) - 1;
    } else if (tool == "save_points") {
        const auto it = run->chunkReads.find(key);
        if (it == run->chunkReads.end())
            return;
        const int index = it.value();
        run->chunkReads.erase(it);
        QJsonObject message = run->messages[index].toObject();
        message["content"] = QStringLiteral("[chunk %1 of %2: read, points saved]").arg(*chunk).arg(importID);
        run->messages[index] = message;
    }
}

void GrokRunner::testConnection(const Configuration &configuration, std::function<void(const ConnectionResult &)> completion)
{
    auto *network = new QNetworkAccessManager;
    const QUrl url = joined(joined(configuration.baseURL, QStringLiteral("models")), configuration.model);
    QNetworkReply *reply = network->get(makeRequest(url, configuration.apiKey, 20'000));
    QObject::connect(reply, &QNetworkReply::finished, network, [=] {
        const HttpReply http = HttpReply::from(reply);
        ConnectionResult result;
        if (http.kind == HttpReply::Kind::ok) {
            result.ok = true;
            result.text = http.object.value("id").isString() ? http.object.value("id").toString() : configuration.model;
        } else if (http.kind == HttpReply::Kind::status && http.code == 404) {
            result.text = QStringLiteral("Grok doesn't know the model “%1”.").arg(configuration.model);
        } else {
            result.text = http.message();
        }
        reply->deleteLater();
        network->deleteLater();
        completion(result);
    });
}

} // namespace wp
