#pragma once
#include "bridge/Bridge.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <functional>
#include <optional>

namespace wp {

/// The MCP server run by `whiteprint-mcp`: JSON-RPC 2.0 over stdio (initialize, tools/list,
/// tools/call, resources/list, resources/read, prompts/list, prompts/get, ping). Lives in the
/// library so it's testable.
///
/// Messages are newline-delimited. stdout carries only protocol lines; logs go to stderr.
/// Tool failures are results with `isError`, not JSON-RPC errors.
class MCPServer
{
public:
    /// Forwards a request to the app (normally `BridgeClient::send`, after making sure the app
    /// is running). May throw BridgeError (reported as an error result).
    using Send = std::function<BridgeResponse(const BridgeRequest &)>;

    static QStringList protocolVersions() { return {"2025-06-18", "2025-03-26", "2024-11-05"}; }
    static QString dslURI() { return QStringLiteral("whiteprint://dsl"); }
    static QString instructions();

    explicit MCPServer(Send send) : send_(std::move(send)) {}

    /// Handles one JSON-RPC message; returns the response line, or nullopt for notifications.
    std::optional<QString> handle(const QString &line) const;

    /// Reads stdin line by line until EOF, writing responses to stdout (both in binary mode,
    /// so no CRLF translation).
    void run() const;

    /// Writes `whiteprint-mcp: message` to stderr.
    static void log(const QString &message);

private:
    std::optional<QJsonObject> respond(const QJsonValue &message) const;
    QJsonObject result(const QString &method, const QJsonObject &params) const;
    QJsonObject callTool(const QJsonObject &params) const;
    QJsonObject prompt(const QJsonObject &params) const;

    Send send_;
};

} // namespace wp
