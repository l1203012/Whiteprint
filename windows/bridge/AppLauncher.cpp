#include "bridge/AppLauncher.h"
#include "bridge/LocalPipe.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

namespace wp::AppLauncher {

QString appExecutable(const QString &helperExecutable)
{
    const QString dir = helperExecutable.isEmpty() ? QCoreApplication::applicationDirPath()
                                                   : QFileInfo(helperExecutable).absolutePath();
    const QFileInfo app(QDir(dir).filePath(BridgePaths::appExecutableName()));
    return app.isFile() ? QDir::toNativeSeparators(app.absoluteFilePath()) : QString();
}

void ensureRunning(const QString &pipeName, double timeoutSeconds)
{
    if (LocalPipe::isAccepting(pipeName))
        return;

    const QString exe = appExecutable();
    if (exe.isEmpty())
        throw BridgeError::launchFailed(QStringLiteral("%1 not found next to whiteprint-mcp.exe")
                                            .arg(BridgePaths::appExecutableName()));
    if (!QProcess::startDetached(exe, {}, QFileInfo(exe).absolutePath()))
        throw BridgeError::launchFailed(QStringLiteral("couldn't run %1").arg(exe));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutSeconds * 1000.0) {
        if (LocalPipe::isAccepting(pipeName))
            return;
        QThread::msleep(100);
    }
    throw BridgeError::launchFailed(
        QStringLiteral("it didn't open its pipe within %1 s").arg(static_cast<int>(timeoutSeconds)));
}

} // namespace wp::AppLauncher
