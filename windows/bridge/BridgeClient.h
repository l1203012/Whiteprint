#pragma once
#include "bridge/Bridge.h"

namespace wp {

/// Used by `whiteprint-mcp`. Blocking; one request at a time. Needs a QCoreApplication, but no
/// running event loop, and may be used from any thread.
///
/// Each `send` opens a fresh connection, so the app may restart between calls. Throws
/// `BridgeError::appNotRunning` when nothing listens on the pipe (the request was not delivered
/// and can be retried), `timedOut` when the app doesn't reply in time.
class BridgeClient
{
public:
    explicit BridgeClient(QString pipeName = BridgePaths::socketName()) : pipeName_(std::move(pipeName)) {}

    BridgeResponse send(const BridgeRequest &request, double timeoutSeconds = 120) const;

    const QString &pipeName() const { return pipeName_; }

private:
    QString pipeName_;
};

} // namespace wp
