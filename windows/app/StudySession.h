#pragma once
// Port of StudySession.swift (the non-UI state behind the study panel).
#include "app/AISettings.h"
#include "bridge/Bridge.h"
#include "study/StudyStore.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QThreadPool>
#include <functional>
#include <optional>

namespace wp {

/// The study panel's state, kept app-wide so a run survives closing the panel: imports in progress,
/// the last errors, and the study plan run (Claude Code or Grok, as chosen in Settings > AI).
/// All members are used on the GUI thread; `changed()` fires after every state change
/// (`studySessionDidChange`). Needs an event loop for imports, `locateClaude` and runs.
class StudySession : public QObject {
    Q_OBJECT
public:
    struct RunState {
        enum class Kind { idle, running, finished, failed };
        Kind kind = Kind::idle;
        /// For failed.
        QString message;
        bool operator==(const RunState &) const = default;
    };

    /// `store` may be null; `tools` answers the Grok runner's tool calls (like the MCP helper's) and
    /// must outlive the session. `store` too.
    StudySession(StudyStore *store, BridgeHandler *tools, QObject *parent = nullptr);
    ~StudySession() override;

    StudyStore *store() const { return m_store; }
    const QList<StudyImport> &imports() const { return m_imports; }
    /// File names being extracted right now.
    const QStringList &extracting() const { return m_extracting; }
    const QStringList &importErrors() const { return m_importErrors; }
    const QStringList &log() const { return m_log; }
    const RunState &runState() const { return m_runState; }
    bool isRunning() const { return m_runState.kind == RunState::Kind::running; }

    /// Whether the `claude` lookup finished, and what it found (nullopt inside = not installed).
    bool claudeLookupDone() const { return m_lookupDone; }
    std::optional<QString> claudePath() const { return m_claudePath; }
    /// Looks for `claude` off the GUI thread; no-op when done already unless `force`.
    void locateClaude(bool force = false);

    // MARK: Imports

    /// Extracts the files one by one off the GUI thread; unsupported files become import errors.
    void importFiles(const QStringList &paths);
    void remove(const QString &importID);
    /// Throws like StudyStore::removeAll.
    void removeAll();
    /// Re-reads the store's imports (also done on `AppEvents::studyStoreDidChange`).
    void refreshImports();

    // MARK: Study plan run

    AIProvider provider() const;
    /// Whether the chosen provider is set up: Claude Code found, or a Grok key saved.
    bool isProviderReady() const;

    /// Starts the chosen provider over every import. The study plan note opens by itself when the
    /// agent calls `build_study_plan`.
    void generate();
    void cancel();

signals:
    void changed();

private:
    void handleEvent(const RunnerEvent &event);
    void start(const QString &line);
    void append(const QString &line);

    StudyStore *m_store;
    BridgeHandler *m_tools;
    QList<StudyImport> m_imports;
    QStringList m_extracting;
    QStringList m_importErrors;
    QStringList m_log;
    RunState m_runState;
    bool m_lookupDone = false;
    bool m_lookupRunning = false;
    std::optional<QString> m_claudePath;
    QPointer<QObject> m_runner;
    std::function<void()> m_cancelRun;
    QThreadPool m_pool;
};

} // namespace wp
