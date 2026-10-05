import Foundation
import WhiteprintBridge
import WhiteprintStudy

/// Gives an in-app agent (the Grok runner) Claude's exact MCP tools, executed
/// by the same handler that answers the MCP helper.
enum AgentToolBridge {
    static func runnerTools(_ tools: [AgentTool] = MCPToolCatalog.tools) -> [RunnerTool] {
        tools.map { RunnerTool(name: $0.name, description: $0.description, inputSchema: $0.inputSchema) }
    }

    /// Maps each tool call to a bridge request and runs it on the main queue.
    /// Failures and invalid arguments come back as error results.
    static func executor(
        handler: BridgeHandler,
        request: @escaping (String, [String: Any]) throws -> BridgeRequest = MCPToolCatalog.request(forTool:arguments:)
    ) -> GrokRunner.ToolExecutor {
        { [weak handler] name, arguments, reply in
            DispatchQueue.main.async {
                guard let handler else { return reply("Whiteprint is shutting down", true) }
                let bridgeRequest: BridgeRequest
                do {
                    bridgeRequest = try request(name, arguments)
                } catch {
                    return reply(errorLine(error), true)
                }
                handler.handle(bridgeRequest) { response in
                    switch response {
                    case .ok(let text): reply(text, false)
                    case .failure(let text): reply(text, true)
                    }
                }
            }
        }
    }
}
