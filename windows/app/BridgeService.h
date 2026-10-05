#pragma once
// Port of BridgeService.swift.
#include "app/NoteRegistry.h"
#include "app/NoteWorkspace.h"
#include "bridge/Bridge.h"
#include "study/StudyStore.h"

#include <QObject>
#include <QThreadPool>

namespace wp {

/// Answers the MCP helper's requests: note requests become Core edits applied through the workspace
/// (synchronously, on the calling thread), study requests run on a thread pool against the study
/// store. Replies are short text meant for Claude; `reply` is called exactly once, possibly from a
/// pool thread. The workspace, registry and store must outlive the service.
class BridgeService : public QObject, public BridgeHandler {
    Q_OBJECT
public:
    /// `study` may be null (the study store couldn't be opened): study requests then fail with a message.
    BridgeService(NoteWorkspace *workspace, NoteRegistry *registry, StudyStore *study, QObject *parent = nullptr);
    ~BridgeService() override;

    void handle(const BridgeRequest &request, std::function<void(BridgeResponse)> reply) override;

    /// Resolves `path` (absolute, or with `~`) to an existing file Whiteprint can extract.
    /// Throws WorkspaceError (fileNotFound / unsupportedFile).
    static QString importableFile(const QString &path);

    /// `i2 · 14 slides · 3 chunks`.
    static QString summary(const StudyImport &item, bool includingID = true);

signals:
    /// Imports or saved points changed; emitted on this object's thread (also forwarded to
    /// `AppEvents::studyStoreDidChange`).
    void studyStoreDidChange();

private:
    QString handleNote(const BridgeRequest &request);
    void handleStudy(const BridgeRequest &request, std::function<void(BridgeResponse)> reply);
    void buildStudyPlan(const BridgeRequest &request, const std::function<void(BridgeResponse)> &reply);
    QString urlFor(const std::optional<QString> &id) const;
    void postStudyChange();

    NoteWorkspace *m_workspace;
    NoteRegistry *m_registry;
    StudyStore *m_study;
    QThreadPool m_pool;
};

} // namespace wp
