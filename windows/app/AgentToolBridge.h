#pragma once
// Port of AgentToolBridge.swift.
#include "bridge/Bridge.h"
#include "study/GrokRunner.h"

#include <QJsonObject>
#include <QList>
#include <functional>

namespace wp {

/// Gives an in-app agent (the Grok runner) Claude's exact MCP tools, executed by the same handler
/// that answers the MCP helper.
namespace AgentToolBridge {

/// Maps `MCPToolCatalog::tools()` (or `tools`) one to one to the runner's tool type.
QList<RunnerTool> runnerTools(const QList<AgentTool> &tools = MCPToolCatalog::tools());

/// Turns a tool call into a request: throws like `MCPToolCatalog::request` (ToolCallError, ArgumentError).
using RequestFn = std::function<BridgeRequest(const QString &name, const QJsonObject &arguments)>;

/// Maps each tool call to a bridge request and hands it to `handler` (called on the runner's thread,
/// which is the GUI thread in the app). Failures and invalid arguments come back as error results.
/// `handler` must outlive the executor.
GrokRunner::ToolExecutor executor(BridgeHandler *handler, RequestFn request = [](const QString &name, const QJsonObject &arguments) {
    return MCPToolCatalog::request(name, arguments);
});

} // namespace AgentToolBridge

} // namespace wp
