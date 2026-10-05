#include "app/AgentToolBridge.h"

#include "app/NoteWorkspace.h"

namespace wp::AgentToolBridge {

QList<RunnerTool> runnerTools(const QList<AgentTool> &tools)
{
    QList<RunnerTool> result;
    result.reserve(tools.size());
    for (const AgentTool &tool : tools)
        result.append({tool.name, tool.description, tool.inputSchema});
    return result;
}

GrokRunner::ToolExecutor executor(BridgeHandler *handler, RequestFn request)
{
    return [handler, request = std::move(request)](const QString &name, const QJsonObject &arguments, GrokRunner::Reply reply) {
        if (!handler)
            return reply(QStringLiteral("Whiteprint is shutting down"), true);
        BridgeRequest bridgeRequest;
        try {
            bridgeRequest = request(name, arguments);
        } catch (...) {
            return reply(currentErrorLine(), true);
        }
        handler->handle(bridgeRequest, [reply](BridgeResponse response) { reply(response.text, !response.isOk); });
    };
}

} // namespace wp::AgentToolBridge
