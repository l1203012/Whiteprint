#pragma once
#include <QString>

namespace wp::LocalPipe {

/// Longest pipe name accepted (Windows allows 256 characters for `\\.\pipe\<name>`).
constexpr int maxNameLength = 200;

/// Throws BridgeError::pathTooLong when `name` is empty or too long.
void validate(const QString &name);

/// Whether something is accepting connections on the pipe `name` right now. Blocks for at
/// most `timeoutMs`. When called on the thread that runs the server, run the event loop
/// afterwards so the server can accept the probe connection.
bool isAccepting(const QString &name, int timeoutMs = 500);

} // namespace wp::LocalPipe
