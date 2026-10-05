#include "app/NoteWorkspace.h"

#include "app/AppUtil.h"
#include "app/NoteDocument.h"
#include "app/NoteFiles.h"
#include "app/NotesFolder.h"
#include "bridge/Bridge.h"
#include "bridge/ToolSchema.h"
#include "core/JsonCoding.h"
#include "core/NoteEditing.h"
#include "core/NoteFormat.h"
#include "extract/Extraction.h"
#include "study/GrokRunner.h"
#include "study/Study.h"

namespace wp {

WorkspaceError::WorkspaceError(Kind kind, QString text, QStringList errors)
    : m_kind(kind), m_text(std::move(text)), m_errors(std::move(errors))
{
    m_what = description().toUtf8();
}

QString WorkspaceError::description() const
{
    switch (m_kind) {
    case Kind::unknownNote: return QStringLiteral("no note '%1' (see list_notes)").arg(m_text);
    case Kind::unreadable: return QStringLiteral("%1 can't be read").arg(m_text);
    case Kind::fileNotFound: return QStringLiteral("no file at %1").arg(m_text);
    case Kind::unsupportedFile:
        return QStringLiteral("%1: unsupported file type (use PDF, DOCX, DOC or PPTX)").arg(m_text);
    case Kind::studyUnavailable: return QStringLiteral("the study store couldn't be opened");
    case Kind::noCards: return QStringLiteral("no cards given; each card needs a question and an answer");
    case Kind::nothingDrawn:
        return (QStringList{QStringLiteral("nothing to draw, not saved:")} + m_errors).join(QLatin1Char('\n'));
    }
    return {};
}

QString currentErrorLine()
{
    try {
        throw;
    } catch (const NoteFormatError &e) {
        if (e.kind == NoteFormatError::Kind::unsupportedVersion)
            return QStringLiteral("note uses format %1; this Whiteprint reads up to %2").arg(e.version).arg(Note::formatVersion);
        return QStringLiteral("note has an invalid format version '%1'").arg(e.text);
    } catch (const NoteEditError &e) {
        return e.description();
    } catch (const WorkspaceError &e) {
        return e.description();
    } catch (const StudyError &e) {
        return e.description();
    } catch (const ExtractionError &e) {
        return e.description();
    } catch (const BridgeError &e) {
        return e.description();
    } catch (const ToolCallError &e) {
        return e.description();
    } catch (const ArgumentError &e) {
        return e.description();
    } catch (const DecodingError &e) {
        return e.message;
    } catch (const GrokError &e) {
        return e.description;
    } catch (const NotesFolder::FolderError &e) {
        return e.description();
    } catch (const NoteFiles::FileError &e) {
        return e.description();
    } catch (const DocumentError &e) {
        return e.description();
    } catch (const FileIOError &e) {
        return e.description();
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    } catch (...) {
        return QStringLiteral("unknown error");
    }
}

QString errorLine(std::exception_ptr error)
{
    try {
        std::rethrow_exception(error);
    } catch (...) {
        return currentErrorLine();
    }
}

} // namespace wp
