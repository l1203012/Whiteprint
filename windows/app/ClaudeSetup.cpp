#include "app/ClaudeSetup.h"

#include "app/AppUtil.h"
#include "study/ClaudeCodeRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>

namespace wp {

namespace ClaudeDesktopConfig {

QString ConfigError::description() const
{
    if (kind == Kind::invalidJSON)
        return QStringLiteral("claude_desktop_config.json isn't valid JSON (%1), so it was left unchanged").arg(detail);
    return QStringLiteral("claude_desktop_config.json isn't a JSON object, so it was left unchanged");
}

QString defaultURL()
{
    QString appData = qEnvironmentVariable("APPDATA");
    if (appData.isEmpty())
        appData = QDir::homePath() + QStringLiteral("/AppData/Roaming");
    return QDir::cleanPath(appData + QStringLiteral("/Claude/claude_desktop_config.json"));
}

QByteArray merging(const QString &helper, const std::optional<QByteArray> &config)
{
    QJsonObject root;
    if (config && !config->trimmed().isEmpty()) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(*config, &error);
        if (error.error != QJsonParseError::NoError)
            throw ConfigError{ConfigError::Kind::invalidJSON, error.errorString()};
        if (!document.isObject())
            throw ConfigError{ConfigError::Kind::notAnObject, {}};
        root = document.object();
    }
    QJsonObject servers = root.value(QStringLiteral("mcpServers")).toObject();
    servers.insert(mcpServerName, QJsonObject{{QStringLiteral("command"), helper}});
    root.insert(QStringLiteral("mcpServers"), servers);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool isConnected(const QString &helper, const QString &configURL)
{
    QFile file(configURL);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;
    const QJsonValue entry = document.object().value(QStringLiteral("mcpServers")).toObject().value(mcpServerName);
    return entry.isObject() && entry.toObject().value(QStringLiteral("command")).toString() == helper;
}

std::optional<QString> connect(const QString &helper, const QString &configURL)
{
    std::optional<QByteArray> existing;
    if (QFileInfo::exists(configURL)) {
        QFile file(configURL);
        if (!file.open(QIODevice::ReadOnly))
            throw FileIOError(QStringLiteral("“%1” can’t be read: %2").arg(QFileInfo(configURL).fileName(), file.errorString()));
        existing = file.readAll();
    }
    const QByteArray merged = merging(helper, existing);
    std::optional<QString> backup;
    if (existing) {
        const QString path = configURL + QStringLiteral(".backup");
        if (QFileInfo::exists(path))
            QFile::remove(path);
        if (!QFile::copy(configURL, path))
            throw FileIOError(QStringLiteral("The backup of “%1” can’t be made.").arg(QFileInfo(configURL).fileName()));
        backup = path;
    }
    QDir().mkpath(deletingLastPathComponent(configURL));
    writeFileAtomically(configURL, merged);
    return backup;
}

QString snippet(const QString &helper)
{
    try {
        return QString::fromUtf8(merging(helper, std::nullopt));
    } catch (...) {
        return {};
    }
}

} // namespace ClaudeDesktopConfig

namespace ClaudeCodeSetup {

QStringList arguments(const QString &helper)
{
    return {QStringLiteral("mcp"), QStringLiteral("add"), QStringLiteral("--scope"), QStringLiteral("user"), mcpServerName,
            QStringLiteral("--"), helper};
}

QString shellQuoted(const QString &word)
{
    static const QString safe = QStringLiteral("-_./\\=:@%+,");
    bool plain = !word.isEmpty();
    for (const QChar c : word)
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && !safe.contains(c))
            plain = false;
    if (plain)
        return word;
    QString escaped = word;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString commandLine(const std::optional<QString> &claude, const QString &helper)
{
    QStringList words{claude.value_or(QStringLiteral("claude"))};
    words += arguments(helper);
    QStringList quoted;
    for (const QString &word : words)
        quoted.append(shellQuoted(word));
    return quoted.join(QLatin1Char(' '));
}

void registerHelper(const QString &claude, const QString &helper, QObject *context, std::function<void(const Result &)> completion)
{
    auto *process = new QProcess(context);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProcessEnvironment(ClaudeCodeRunner::environment(claude));
    const QString suffix = QFileInfo(claude).suffix().toLower();
    if (suffix == QLatin1String("cmd") || suffix == QLatin1String("bat")) {
        process->setProgram(qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe")));
#ifdef Q_OS_WIN
        QStringList words{QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(claude))};
        for (const QString &argument : arguments(helper))
            words.append(shellQuoted(argument));
        process->setNativeArguments(QStringLiteral("/d /s /c \"%1\"").arg(words.join(QLatin1Char(' '))));
#endif
    } else {
        process->setProgram(claude);
        process->setArguments(arguments(helper));
    }
    QPointer<QObject> guard(context);
    auto finish = [process, guard, completion](const Result &result) {
        process->deleteLater();
        if (guard && completion)
            completion(result);
    };
    QObject::connect(process, &QProcess::errorOccurred, process, [process, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish({false, process->errorString()});
    });
    QObject::connect(process, &QProcess::finished, process, [process, finish](int exitCode, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(process->readAll()).trimmed();
        if (status == QProcess::NormalExit && exitCode == 0)
            finish({true, output});
        else
            finish({false, output.isEmpty() ? QStringLiteral("claude exited with status %1").arg(exitCode) : output});
    });
    process->start();
}

} // namespace ClaudeCodeSetup

} // namespace wp
