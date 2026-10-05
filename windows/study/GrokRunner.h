#pragma once
// Port of GrokRunner.swift.
#include "study/RunnerEvent.h"
#include "study/StudyStore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace wp {

/// A tool offered to an API-based agent: name, description, JSON Schema.
struct RunnerTool {
    QString name;
    QString description;
    QJsonObject inputSchema;
};

/// A Grok failure, described in one line for the user.
struct GrokError {
    QString description;
    bool operator==(const GrokError &) const = default;
};

/// Builds the study plan with xAI's Grok API instead of Claude Code, for people who'd rather use an API
/// key. Runs the same tool loop as Claude: the app executes each tool call through the same handler the
/// MCP helper uses.
///
/// Talks to the OpenAI-compatible `POST {baseURL}/chat/completions`. The API is stateless, so every turn
/// resends the history; to keep it small, a chunk's `read_chunk` text is replaced by a placeholder once
/// its points are saved.
///
/// Lives on the thread that created it (normally the GUI thread); requests are asynchronous and events
/// and tool calls happen on that thread.
class GrokRunner : public QObject {
    Q_OBJECT
public:
    struct Configuration {
        QString apiKey;
        QString model = defaultModel();
        QUrl baseURL = defaultBaseURL();

        /// xAI's recommended general model (docs.x.ai, October 2026).
        static QString defaultModel() { return QStringLiteral("grok-4.7"); }
        static QUrl defaultBaseURL() { return QUrl(QStringLiteral("https://api.x.ai/v1")); }
        bool operator==(const Configuration &) const = default;
    };

    using Event = RunnerEvent;
    using EventHandler = std::function<void(const Event &)>;
    /// Called when a tool result is ready; `(text, isError)`. Safe to call from any thread, exactly once.
    using Reply = std::function<void(const QString &text, bool isError)>;
    /// Runs one tool call (on the runner's thread); the executor calls `reply` when done, immediately or later.
    using ToolExecutor = std::function<void(const QString &name, const QJsonObject &arguments, Reply reply)>;

    /// `store` (optional, must outlive the runner) names files and chunk counts in progress messages.
    GrokRunner(Configuration configuration, QList<RunnerTool> tools, ToolExecutor execute, StudyStore *store = nullptr,
               QObject *parent = nullptr);
    ~GrokRunner() override;

    /// A run stops with an error after this many tool calls.
    int maxToolCalls = 250;
    /// Requests are tried this often when Grok is busy (429) or failing (5xx).
    static constexpr int maxAttempts = 3;
    /// Seconds before retry `n` (1-based) when Grok sends no Retry-After: 1, 2, 4... times this.
    double backoff = 1;

    bool isRunning() const { return m_run != nullptr; }

    /// Starts the tool loop for the given imports (all when empty). `onEvent` is called on this object's
    /// thread, ending with exactly one `finished` or `failed`. Throws StudyError::alreadyRunning while a
    /// run is in progress.
    void start(const QStringList &importIDs, EventHandler onEvent);

    /// Stops the run, including any request in flight. The run then ends with `failed("Cancelled")`.
    void cancel();

    /// Outcome of `testConnection`: the model id on success, else a one-line message.
    struct ConnectionResult {
        bool ok = false;
        QString text;
    };
    /// Checks the key and model by looking the model up (`GET {baseURL}/models/{model}`), which uses no
    /// tokens. `completion` is called on the calling thread (which needs an event loop).
    static void testConnection(const Configuration &configuration, std::function<void(const ConnectionResult &)> completion);

private:
    struct Run;
    struct HttpReply;

    void end(const std::shared_ptr<Run> &run, const Event &event);
    void send(const std::shared_ptr<Run> &run, int attempt = 1);
    void received(const HttpReply &reply, const std::shared_ptr<Run> &run, int attempt);
    void answered(const QJsonObject &message, const std::shared_ptr<Run> &run);
    void runTools(const QJsonArray &calls, int from, const std::shared_ptr<Run> &run);
    void record(const QString &text, bool isError, const QString &id, const std::shared_ptr<Run> &run);
    void trimHistory(const QString &tool, const QJsonObject &arguments, const std::shared_ptr<Run> &run);

    Configuration m_configuration;
    QJsonArray m_tools;
    ToolExecutor m_execute;
    StudyStore *m_store;
    QNetworkAccessManager *m_network;
    std::shared_ptr<Run> m_run;
};

} // namespace wp
