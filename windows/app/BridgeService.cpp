#include "app/BridgeService.h"

#include "app/AppEvents.h"
#include "app/AppUtil.h"
#include "app/NoteTitle.h"
#include "core/DrawingCompiler.h"
#include "core/TextUtil.h"
#include "extract/Extraction.h"
#include "study/StudyPlanRenderer.h"

#include <QFileInfo>
#include <QMetaObject>

namespace wp {

namespace {

BridgeResponse respond(const std::function<QString()> &body)
{
    try {
        return BridgeResponse::ok(body());
    } catch (...) {
        return BridgeResponse::failure(currentErrorLine());
    }
}

QStringList compileErrors(const QString &dsl)
{
    QStringList lines;
    for (const auto &error : DrawingCompiler::compile(dsl).errors)
        lines.append(error.description());
    return lines;
}

/// Refuses source where every statement failed, so a typo can't replace a drawing with an empty one.
void rejectIfNothingDrawn(const QString &dsl)
{
    const CompiledDrawing compiled = DrawingCompiler::compile(dsl);
    if (compiled.errors.isEmpty() || !(compiled.scene == DrawingScene()))
        return;
    QStringList errors;
    for (const auto &error : compiled.errors)
        errors.append(error.description());
    throw WorkspaceError::nothingDrawn(errors);
}

/// `ok d3` followed by one `line N: ...` per compile error.
QString withCompileErrors(const QString &head, const QString &dsl)
{
    return (QStringList{head} + compileErrors(dsl)).join(QLatin1Char('\n'));
}

} // namespace

BridgeService::BridgeService(NoteWorkspace *workspace, NoteRegistry *registry, StudyStore *study, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_registry(registry), m_study(study)
{
}

BridgeService::~BridgeService()
{
    m_pool.waitForDone();
}

void BridgeService::handle(const BridgeRequest &request, std::function<void(BridgeResponse)> reply)
{
    switch (request.kind) {
    case BridgeRequest::Kind::importDocument:
    case BridgeRequest::Kind::listImports:
    case BridgeRequest::Kind::readChunk:
    case BridgeRequest::Kind::savePoints:
    case BridgeRequest::Kind::getPoints:
        handleStudy(request, std::move(reply));
        break;
    case BridgeRequest::Kind::buildStudyPlan:
        buildStudyPlan(request, reply);
        break;
    default:
        reply(respond([&] { return handleNote(request); }));
    }
}

QString BridgeService::urlFor(const std::optional<QString> &id) const
{
    const QString text = id.value_or(QString());
    if (const auto url = m_registry->url(text))
        return *url;
    throw WorkspaceError::unknownNote(text);
}

// MARK: Notes

QString BridgeService::handleNote(const BridgeRequest &request)
{
    using Kind = BridgeRequest::Kind;
    switch (request.kind) {
    case Kind::ping:
        return QStringLiteral("pong");

    case Kind::listNotes: {
        QStringList lines;
        for (const QString &url : m_workspace->noteURLs()) {
            const QString id = m_registry->id(url);
            std::optional<Note> note;
            try {
                note = m_workspace->note(url);
            } catch (...) {
            }
            if (!note) {
                lines.append(QStringLiteral("%1 \"%2\" · unreadable").arg(id, NoteTitle::baseName(url)));
                continue;
            }
            const int pages = int(note->pages().size());
            const QString path = m_workspace->folderPath(url).value_or(QString());
            const QString folder = path.isEmpty() ? QString() : QStringLiteral(" · ") + path;
            lines.append(QStringLiteral("%1 \"%2\"%3 · %4 page%5")
                             .arg(id, NoteTitle::display(*note, url), folder)
                             .arg(pages)
                             .arg(pages == 1 ? QString() : QStringLiteral("s")));
        }
        return lines.isEmpty() ? QStringLiteral("no notes") : lines.join(QLatin1Char('\n'));
    }

    case Kind::readNote: {
        const Note note = m_workspace->note(urlFor(request.note));
        if (request.page) {
            const QString source = note.pageSource(*request.page);
            return source.isEmpty() ? QStringLiteral("(empty page)") : source;
        }
        QStringList parts;
        for (int page = 1; page <= note.pages().size(); ++page)
            parts.append(QStringLiteral("=== page %1 ===\n%2").arg(page).arg(note.pageSource(page)));
        return parts.join(QStringLiteral("\n\n"));
    }

    case Kind::createNote: {
        Note note(request.title);
        if (request.markdown && !request.markdown->isEmpty())
            note.write(*request.markdown, 1, WriteMode::replace);
        const QString url = m_workspace->createNote(note, request.title, request.folder);
        return QStringLiteral("ok %1").arg(m_registry->id(url));
    }

    case Kind::createFlashcards: {
        if (request.cards.isEmpty())
            throw WorkspaceError::noCards();
        const CardDeck deck(QString(), request.title, request.cards);
        if (!request.note) {
            const QString title = QStringLiteral("Flashcards – %1").arg(request.title);
            Note note(title);
            const QString deckID = note.insertDeck(deck, 1);
            const QString url = m_workspace->createNote(note, title, std::nullopt);
            return QStringLiteral("ok %1 %2").arg(m_registry->id(url), deckID);
        }
        const QString deckID = m_workspace->edit<QString>(urlFor(request.note), QStringLiteral("Claude’s Flashcards"), [&](Note &note) {
            return note.insertDeck(deck, request.page.value_or(int(note.pages().size())));
        });
        return QStringLiteral("ok %1 %2").arg(trimmedWhitespace(*request.note).toLower(), deckID);
    }

    case Kind::write:
        m_workspace->edit(urlFor(request.note), QStringLiteral("Claude’s Edit"), [&](Note &note) {
            note.write(request.markdown.value_or(QString()), request.page.value_or(1), request.mode);
        });
        return QStringLiteral("ok");

    case Kind::draw: {
        rejectIfNothingDrawn(request.dsl);
        const QString id = m_workspace->edit<QString>(urlFor(request.note), QStringLiteral("Claude’s Drawing"), [&](Note &note) {
            return note.insertDrawing(request.dsl, request.page.value_or(1), request.after);
        });
        return withCompileErrors(QStringLiteral("ok %1").arg(id), request.dsl);
    }

    case Kind::editDrawing:
        rejectIfNothingDrawn(request.dsl);
        m_workspace->edit(urlFor(request.note), QStringLiteral("Claude’s Drawing"),
                          [&](Note &note) { note.updateDrawing(request.drawing, request.dsl); });
        return withCompileErrors(QStringLiteral("ok"), request.dsl);

    case Kind::deleteDrawing:
        m_workspace->edit(urlFor(request.note), QStringLiteral("Delete Drawing"),
                          [&](Note &note) { note.deleteDrawing(request.drawing); });
        return QStringLiteral("ok");

    case Kind::addPage: {
        const int page = m_workspace->edit<int>(urlFor(request.note), QStringLiteral("Add Page"),
                                                [&](Note &note) { return note.addPage(); });
        return QStringLiteral("ok %1").arg(page);
    }

    default:
        Q_UNREACHABLE();
    }
}

// MARK: Study

void BridgeService::handleStudy(const BridgeRequest &request, std::function<void(BridgeResponse)> reply)
{
    using Kind = BridgeRequest::Kind;
    if (!m_study)
        return reply(BridgeResponse::failure(WorkspaceError::studyUnavailable().description()));
    StudyStore *study = m_study;
    if (request.kind == Kind::importDocument) {
        QString file;
        try {
            file = importableFile(request.path);
        } catch (...) {
            return reply(BridgeResponse::failure(currentErrorLine()));
        }
        m_pool.start([this, study, file, reply] {
            reply(respond([&] {
                const StudyImport item = study->importFile(file);
                postStudyChange();
                return QStringLiteral("ok ") + summary(item);
            }));
        });
        return;
    }
    m_pool.start([this, study, request, reply] {
        reply(respond([&]() -> QString {
            switch (request.kind) {
            case Kind::listImports: {
                QStringList lines;
                for (const StudyImport &item : study->imports())
                    lines.append(QStringLiteral("%1 %2 · %3 · %4/%5 done")
                                     .arg(item.id, item.name, summary(item, false))
                                     .arg(item.chunksDone)
                                     .arg(item.chunkCount));
                return lines.isEmpty() ? QStringLiteral("no imports") : lines.join(QLatin1Char('\n'));
            }
            case Kind::readChunk:
                return study->chunk(request.importID.value_or(QString()), request.chunk);
            case Kind::savePoints:
                study->savePoints(request.points, request.importID.value_or(QString()), request.chunk);
                postStudyChange();
                return QStringLiteral("ok");
            case Kind::getPoints: {
                const QString text = study->pointsSummary(request.importID);
                return text.isEmpty() ? QStringLiteral("no points saved") : text;
            }
            default:
                Q_UNREACHABLE();
            }
        }));
    });
}

void BridgeService::buildStudyPlan(const BridgeRequest &request, const std::function<void(BridgeResponse)> &reply)
{
    if (!m_study)
        return reply(BridgeResponse::failure(WorkspaceError::studyUnavailable().description()));
    QSet<QString> known;
    for (const StudyImport &item : m_study->imports())
        known.insert(item.id);
    for (const QString &id : request.importIDs)
        if (!known.contains(id))
            return reply(BridgeResponse::failure(StudyError::unknownImport(id).description()));
    reply(respond([&] {
        const Note note = StudyPlanRenderer::note(request.plan);
        const QString url = m_workspace->createNote(note, QStringLiteral("Study plan – %1").arg(request.plan.title), std::nullopt);
        return QStringLiteral("ok %1").arg(m_registry->id(url));
    }));
}

QString BridgeService::importableFile(const QString &path)
{
    const QString file = expandingTildeInPath(path.trimmed());
    const QFileInfo info(file);
    if (!info.exists() || info.isDir())
        throw WorkspaceError::fileNotFound(path);
    if (!DocumentExtractor::supportedExtensions().contains(info.suffix().toLower()))
        throw WorkspaceError::unsupportedFile(info.fileName());
    return file;
}

QString BridgeService::summary(const StudyImport &item, bool includingID)
{
    const QString extension = QFileInfo(item.name).suffix().toLower();
    const QString unit = extension == QLatin1String("pptx") ? QStringLiteral("slide")
        : extension == QLatin1String("pdf")                 ? QStringLiteral("page")
                                                            : QStringLiteral("section");
    QStringList parts;
    if (includingID)
        parts.append(item.id);
    parts.append(QStringLiteral("%1 %2%3").arg(item.unitCount).arg(unit, item.unitCount == 1 ? QString() : QStringLiteral("s")));
    parts.append(QStringLiteral("%1 chunk%2").arg(item.chunkCount).arg(item.chunkCount == 1 ? QString() : QStringLiteral("s")));
    return parts.join(QStringLiteral(" · "));
}

void BridgeService::postStudyChange()
{
    QMetaObject::invokeMethod(
        this,
        [this] {
            emit studyStoreDidChange();
            emit AppEvents::instance().studyStoreDidChange();
        },
        Qt::QueuedConnection);
}

} // namespace wp
