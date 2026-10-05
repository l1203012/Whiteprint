// CONTRACT (owner: bridge agent): stdio MCP server forwarding to the app.
import WhiteprintBridge

MCPServer(send: { try BridgeClient().send($0) }).run()
