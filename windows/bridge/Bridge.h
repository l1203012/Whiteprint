#pragma once
#include "core/CardDeck.h"
#include "core/NoteEditing.h"
#include "core/StudyModel.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <exception>
#include <functional>
#include <optional>
#include <string>

namespace wp {

// The app owns all state. `whiteprint-mcp` (spawned by Claude Code / Claude Desktop over stdio)
// forwards each MCP tool call to the running app as one BridgeRequest over a named pipe
// (QLocalSocket) and returns the reply text.
// Wire format: one JSON object per line in each direction, identical to the macOS app's
// synthesized Codable: `{"readNote":{"note":"n1","page":2}}`, `{"ping":{}}`, `{"ok":{"_0":"text"}}`.

/// Note ids (`n1`, `n2`, ...) are assigned by the app per session and listed by `listNotes`.
/// Page numbers are 1-based. Drawing ids are per note.
///
/// A tagged struct instead of Swift's enum with payloads; build values with the factory
/// functions, which fill exactly the fields the case uses.
struct BridgeRequest {
    enum class Kind {
        ping,
        listNotes,
        readNote,
        createNote,
        write,
        draw,
        editDrawing,
        deleteDrawing,
        addPage,
        importDocument,
        listImports,
        readChunk,
        savePoints,
        getPoints,
        buildStudyPlan,
        createFlashcards,
    };

    Kind kind = Kind::ping;
    /// readNote, write, draw, editDrawing, deleteDrawing, addPage; optional for createFlashcards.
    std::optional<QString> note;
    /// readNote (optional), write, draw (required); createFlashcards (optional).
    std::optional<int> page;
    /// createNote, createFlashcards.
    QString title;
    /// createNote (optional), write.
    std::optional<QString> markdown;
    /// createNote: relative to the notes folder (e.g. `Courses/Networks`); nullopt = top level.
    std::optional<QString> folder;
    WriteMode mode = WriteMode::append;
    /// draw, editDrawing.
    QString dsl;
    /// draw: id of the drawing to insert after.
    std::optional<QString> after;
    /// editDrawing, deleteDrawing.
    QString drawing;
    /// importDocument.
    QString path;
    /// readChunk, savePoints (required); getPoints (optional).
    std::optional<QString> importID;
    int chunk = 0;
    QList<StudyPoint> points;
    /// buildStudyPlan.
    QStringList importIDs;
    StudyPlan plan;
    /// createFlashcards.
    QList<Flashcard> cards;

    bool operator==(const BridgeRequest &) const = default;

    static BridgeRequest ping();
    static BridgeRequest listNotes();
    static BridgeRequest readNote(QString note, std::optional<int> page = std::nullopt);
    static BridgeRequest createNote(QString title, std::optional<QString> markdown = std::nullopt,
                                    std::optional<QString> folder = std::nullopt);
    static BridgeRequest write(QString note, int page, QString markdown, WriteMode mode);
    static BridgeRequest draw(QString note, int page, QString dsl, std::optional<QString> after = std::nullopt);
    static BridgeRequest editDrawing(QString note, QString drawing, QString dsl);
    static BridgeRequest deleteDrawing(QString note, QString drawing);
    static BridgeRequest addPage(QString note);
    static BridgeRequest importDocument(QString path);
    static BridgeRequest listImports();
    static BridgeRequest readChunk(QString importID, int chunk);
    static BridgeRequest savePoints(QString importID, int chunk, QList<StudyPoint> points);
    static BridgeRequest getPoints(std::optional<QString> importID = std::nullopt);
    static BridgeRequest buildStudyPlan(QStringList importIDs, StudyPlan plan);
    /// Adds a deck to `note` (at the end of `page`, default the last page), or creates a new
    /// note for it when `note` is nullopt. Reply: `ok n4 c1`.
    static BridgeRequest createFlashcards(std::optional<QString> note, std::optional<int> page, QString title,
                                          QList<Flashcard> cards);

    /// The case name used as the JSON key, e.g. `readNote`.
    QString caseName() const;
    QJsonObject toJson() const;
    /// Throws DecodingError (core/JsonCoding.h) when the JSON is not a request.
    static BridgeRequest fromJson(const QJsonObject &object);
    /// Compact JSON, for logs and tests.
    QString description() const;
};

/// Replies are short text, passed straight to Claude as the tool result.
struct BridgeResponse {
    bool isOk = true;
    QString text;

    bool operator==(const BridgeResponse &) const = default;

    static BridgeResponse ok(QString text) { return {true, std::move(text)}; }
    static BridgeResponse failure(QString text) { return {false, std::move(text)}; }

    QJsonObject toJson() const;
    /// Throws DecodingError.
    static BridgeResponse fromJson(const QJsonObject &object);
};

/// Implemented by the app. Called on the server's thread (the main thread in the app);
/// call `reply` exactly once, from any thread. A second call is ignored.
class BridgeHandler
{
public:
    virtual ~BridgeHandler() = default;
    virtual void handle(const BridgeRequest &request, std::function<void(BridgeResponse)> reply) = 0;
};

/// Errors from the pipe transport and from launching the app.
class BridgeError : public std::exception
{
public:
    enum class Kind {
        /// Nothing is listening on the pipe.
        appNotRunning,
        /// The app accepted the request but didn't reply in time.
        timedOut,
        /// Another process is already serving the pipe.
        alreadyRunning,
        /// The pipe name exceeds the Windows limit.
        pathTooLong,
        /// The app couldn't be launched, or didn't open its pipe in time.
        launchFailed,
        /// The connection closed early or the reply wasn't a `BridgeResponse`.
        badReply,
        /// A system call failed.
        system,
    };

    Kind kind = Kind::appNotRunning;
    /// Pipe name, reason, or failed call, depending on `kind`.
    QString detail;
    /// For `timedOut`.
    double seconds = 0;
    /// For `system`: the reason text.
    QString reason;

    static BridgeError appNotRunning();
    static BridgeError timedOut(double seconds);
    static BridgeError alreadyRunning(QString name);
    static BridgeError pathTooLong(QString name);
    static BridgeError launchFailed(QString reason);
    static BridgeError badReply(QString reason);
    static BridgeError system(QString call, QString reason);

    bool operator==(const BridgeError &other) const
    {
        return kind == other.kind && detail == other.detail && seconds == other.seconds && reason == other.reason;
    }

    QString description() const;
    const char *what() const noexcept override;

private:
    mutable std::string whatCache_;
};

class BridgePaths
{
public:
    /// Environment variable that overrides `socketName()`, for tests and debugging.
    static QString socketEnvironmentKey() { return QStringLiteral("WHITEPRINT_SOCKET"); }

    /// `Whiteprint.exe`, the app's file name.
    static QString appExecutableName() { return QStringLiteral("Whiteprint.exe"); }

    /// `%LOCALAPPDATA%\Whiteprint`, created on first use.
    static QString supportDirectory();

    /// The pipe name (served as `\\.\pipe\<name>`): `$WHITEPRINT_SOCKET` when set (path separators
    /// replaced), else `whiteprint-<username>`.
    static QString socketName();

    /// `whiteprint-mcp.exe` next to the running executable.
    static QString helperExecutable();
};

/// One Whiteprint tool as offered to a model: in MCP `tools/list`, and to in-app agents so they
/// get Claude's exact tools.
struct AgentTool {
    QString name;
    QString description;
    /// JSON Schema object, as sent in MCP `tools/list`.
    QJsonObject inputSchema;
    /// MCP hints such as `readOnlyHint`; not part of other APIs' tool formats.
    QMap<QString, bool> annotations;
};

/// Why a tool call couldn't become a request. The description is one line, meant to be shown to
/// the model as the tool result.
struct ToolCallError {
    enum class Kind { unknownTool, invalidArguments };
    Kind kind = Kind::unknownTool;
    /// The tool name, or the problem (e.g. `points[0].ref: required`).
    QString detail;

    bool operator==(const ToolCallError &) const = default;

    static ToolCallError unknownTool(QString name) { return {Kind::unknownTool, std::move(name)}; }
    static ToolCallError invalidArguments(QString problem) { return {Kind::invalidArguments, std::move(problem)}; }

    QString description() const;
};

/// The single source of truth for Whiteprint's tools: what `tools/list` serves and how
/// `tools/call` validates arguments.
namespace MCPToolCatalog {

/// Every Whiteprint tool, in `tools/list` order.
QList<AgentTool> tools();

/// Validates `arguments` (a decoded JSON object) for tool `name` and maps them to a request.
/// Throws a `ToolCallError`.
BridgeRequest request(const QString &forTool, const QJsonObject &arguments);

} // namespace MCPToolCatalog

} // namespace wp
