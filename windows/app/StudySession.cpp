#include "app/StudySession.h"

#include "app/AgentToolBridge.h"
#include "app/AppEvents.h"
#include "app/NoteWorkspace.h"
#include "extract/Extraction.h"
#include "study/ClaudeCodeRunner.h"
#include "study/GrokRunner.h"

#include <QFileInfo>
#include <QMetaObject>

namespace wp {

namespace {
constexpr int logLimit = 200;
}

StudySession::StudySession(StudyStore *store, BridgeHandler *tools, QObject *parent)
    : QObject(parent), m_store(store), m_tools(tools)
{
    m_pool.setMaxThreadCount(1);
    if (m_store)
        m_imports = m_store->imports();
    connect(&AppEvents::instance(), &AppEvents::studyStoreDidChange, this, &StudySession::refreshImports);
}

StudySession::~StudySession()
{
    m_pool.waitForDone();
}

void StudySession::locateClaude(bool force)
{
    if ((m_lookupDone && !force) || m_lookupRunning)
        return;
    m_lookupRunning = true;
    QPointer<StudySession> self(this);
    m_pool.start([self] {
        const auto path = ClaudeCodeRunner::locateClaude();
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, path] {
            if (!self)
                return;
            self->m_lookupRunning = false;
            self->m_lookupDone = true;
            self->m_claudePath = path;
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

// MARK: Imports

void StudySession::importFiles(const QStringList &paths)
{
    if (!m_store) {
        m_importErrors = {WorkspaceError::studyUnavailable().description()};
        return emit changed();
    }
    m_importErrors.clear();
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!DocumentExtractor::supportedExtensions().contains(info.suffix().toLower())) {
            m_importErrors.append(WorkspaceError::unsupportedFile(info.fileName()).description());
            continue;
        }
        m_extracting.append(info.fileName());
        StudyStore *store = m_store;
        QPointer<StudySession> self(this);
        m_pool.start([self, store, path] {
            QString failure;
            try {
                store->importFile(path);
            } catch (...) {
                failure = currentErrorLine();
            }
            if (!self)
                return;
            QMetaObject::invokeMethod(self.data(), [self, path, failure] {
                if (!self)
                    return;
                const QString name = QFileInfo(path).fileName();
                const int index = int(self->m_extracting.indexOf(name));
                if (index >= 0)
                    self->m_extracting.removeAt(index);
                if (!failure.isEmpty())
                    self->m_importErrors.append(failure);
                self->refreshImports();
            }, Qt::QueuedConnection);
        });
    }
    emit changed();
}

void StudySession::remove(const QString &importID)
{
    try {
        if (m_store)
            m_store->remove(importID);
    } catch (...) {
        m_importErrors = {currentErrorLine()};
    }
    refreshImports();
}

void StudySession::removeAll()
{
    if (m_store)
        m_store->removeAll();
    refreshImports();
}

void StudySession::refreshImports()
{
    m_imports = m_store ? m_store->imports() : QList<StudyImport>();
    emit changed();
}

// MARK: Study plan run

AIProvider StudySession::provider() const
{
    return AISettings::shared().provider();
}

bool StudySession::isProviderReady() const
{
    switch (provider()) {
    case AIProvider::claudeCode: return m_lookupDone && m_claudePath.has_value();
    case AIProvider::grok: return AISettings::shared().grokConfiguration().has_value();
    }
    return false;
}

void StudySession::generate()
{
    if (isRunning())
        return;
    QStringList ids;
    for (const StudyImport &item : m_imports)
        ids.append(item.id);
    const auto onEvent = [this](const RunnerEvent &event) { handleEvent(event); };
    try {
        switch (provider()) {
        case AIProvider::claudeCode: {
            if (!m_lookupDone || !m_claudePath)
                return;
            auto *runner = new ClaudeCodeRunner(*m_claudePath, m_store, this);
            m_runner = runner;
            start(QStringLiteral("Starting Claude Code…"));
            runner->start(ids, BridgePaths::helperExecutable(), onEvent);
            m_cancelRun = [runner] { runner->cancel(); };
            break;
        }
        case AIProvider::grok: {
            const auto configuration = AISettings::shared().grokConfiguration();
            if (!configuration || !m_tools)
                return;
            auto *runner = new GrokRunner(*configuration, AgentToolBridge::runnerTools(), AgentToolBridge::executor(m_tools), m_store, this);
            m_runner = runner;
            start(QStringLiteral("Starting Grok (%1)…").arg(configuration->model));
            runner->start(ids, onEvent);
            m_cancelRun = [runner] { runner->cancel(); };
            break;
        }
        }
    } catch (...) {
        if (m_runner)
            m_runner->deleteLater();
        m_cancelRun = nullptr;
        const QString line = currentErrorLine();
        m_runState = {RunState::Kind::failed, line};
        m_log.append(line);
    }
    emit changed();
}

void StudySession::start(const QString &line)
{
    m_log = {line};
    m_runState = {RunState::Kind::running, {}};
}

void StudySession::cancel()
{
    if (!isRunning())
        return;
    if (m_cancelRun)
        m_cancelRun();
    m_cancelRun = nullptr;
    m_runState = {RunState::Kind::idle, {}};
    append(QStringLiteral("Cancelled."));
}

void StudySession::handleEvent(const RunnerEvent &event)
{
    // A cancelled run still reports its failure; the session already went idle.
    if (!isRunning() && event.kind != RunnerEvent::Kind::status && event.kind != RunnerEvent::Kind::text) {
        m_cancelRun = nullptr;
        if (m_runner)
            m_runner->deleteLater();
        return;
    }
    switch (event.kind) {
    case RunnerEvent::Kind::status:
        append(event.message);
        break;
    case RunnerEvent::Kind::text: {
        const QString line = event.message.trimmed();
        if (!line.isEmpty())
            append(line);
        break;
    }
    case RunnerEvent::Kind::finished:
        m_cancelRun = nullptr;
        m_runState = {RunState::Kind::finished, {}};
        append(QStringLiteral("Done. The study plan opened in a new tab."));
        if (m_runner)
            m_runner->deleteLater();
        break;
    case RunnerEvent::Kind::failed:
        m_cancelRun = nullptr;
        m_runState = {RunState::Kind::failed, event.message};
        append(event.message);
        if (m_runner)
            m_runner->deleteLater();
        break;
    }
}

void StudySession::append(const QString &line)
{
    m_log.append(line);
    if (m_log.size() > logLimit)
        m_log.remove(0, m_log.size() - logLimit);
    emit changed();
}

} // namespace wp
