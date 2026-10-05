// stdio MCP server forwarding each tool call to the running app. Spawned by
// Claude Code / Claude Desktop; starts the app on the first call if needed.
// Set WHITEPRINT_SOCKET to talk to a different socket (tests, debugging).
import WhiteprintBridge

let client = BridgeClient()

MCPServer(send: { request in
    do {
        return try client.send(request)
    } catch BridgeError.appNotRunning {
        try AppLauncher.ensureRunning()
        return try client.send(request)
    }
}).run()
