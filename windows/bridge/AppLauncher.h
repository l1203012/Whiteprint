#pragma once
#include "bridge/Bridge.h"

namespace wp {

/// Starts Whiteprint when `whiteprint-mcp` finds nothing listening on the pipe.
namespace AppLauncher {

/// Returns once the app accepts connections on `pipeName`. If it isn't running, starts the
/// `Whiteprint.exe` next to this executable and waits up to `timeoutSeconds` for the pipe.
/// Throws BridgeError::launchFailed.
void ensureRunning(const QString &pipeName = BridgePaths::socketName(), double timeoutSeconds = 10);

/// `Whiteprint.exe` in the directory of `helperExecutable` (or of the running executable when
/// empty), or an empty string if there is no such file.
QString appExecutable(const QString &helperExecutable = {});

} // namespace AppLauncher

} // namespace wp
