// stdio MCP server forwarding each tool call to the running app. Spawned by Claude Code /
// Claude Desktop; starts the app on the first call if needed. Console app, no GUI.
// Set WHITEPRINT_SOCKET to talk to a different pipe (tests, debugging).
#include "bridge/AppLauncher.h"
#include "bridge/BridgeClient.h"
#include "bridge/MCPServer.h"

#include <QCoreApplication>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // stdout carries the protocol: no CRLF translation in either direction.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QCoreApplication app(argc, argv);
#ifdef WP_VERSION
    QCoreApplication::setApplicationVersion(QStringLiteral(WP_VERSION));
#endif

    const wp::BridgeClient client;
    wp::MCPServer server([&client](const wp::BridgeRequest &request) {
        try {
            return client.send(request);
        } catch (const wp::BridgeError &e) {
            if (e.kind != wp::BridgeError::Kind::appNotRunning)
                throw;
            wp::AppLauncher::ensureRunning();
            return client.send(request);
        }
    });
    server.run();
    return 0;
}
