#include "bridge/Bridge.h"
#include "core/JsonCoding.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

namespace wp {

namespace {

struct KindName {
    BridgeRequest::Kind kind;
    const char *name;
};

constexpr KindName kKindNames[] = {
    {BridgeRequest::Kind::ping, "ping"},
    {BridgeRequest::Kind::listNotes, "listNotes"},
    {BridgeRequest::Kind::readNote, "readNote"},
    {BridgeRequest::Kind::createNote, "createNote"},
    {BridgeRequest::Kind::write, "write"},
    {BridgeRequest::Kind::draw, "draw"},
    {BridgeRequest::Kind::editDrawing, "editDrawing"},
    {BridgeRequest::Kind::deleteDrawing, "deleteDrawing"},
    {BridgeRequest::Kind::addPage, "addPage"},
    {BridgeRequest::Kind::importDocument, "importDocument"},
    {BridgeRequest::Kind::listImports, "listImports"},
    {BridgeRequest::Kind::readChunk, "readChunk"},
    {BridgeRequest::Kind::savePoints, "savePoints"},
    {BridgeRequest::Kind::getPoints, "getPoints"},
    {BridgeRequest::Kind::buildStudyPlan, "buildStudyPlan"},
    {BridgeRequest::Kind::createFlashcards, "createFlashcards"},
};

void put(QJsonObject &o, const char *key, const std::optional<QString> &value)
{
    if (value)
        o.insert(QLatin1String(key), *value);
}

void put(QJsonObject &o, const char *key, const std::optional<int> &value)
{
    if (value)
        o.insert(QLatin1String(key), *value);
}

} // namespace

// MARK: BridgeRequest

BridgeRequest BridgeRequest::ping() { return {}; }

BridgeRequest BridgeRequest::listNotes()
{
    BridgeRequest r;
    r.kind = Kind::listNotes;
    return r;
}

BridgeRequest BridgeRequest::readNote(QString note, std::optional<int> page)
{
    BridgeRequest r;
    r.kind = Kind::readNote;
    r.note = std::move(note);
    r.page = page;
    return r;
}

BridgeRequest BridgeRequest::createNote(QString title, std::optional<QString> markdown, std::optional<QString> folder)
{
    BridgeRequest r;
    r.kind = Kind::createNote;
    r.title = std::move(title);
    r.markdown = std::move(markdown);
    r.folder = std::move(folder);
    return r;
}

BridgeRequest BridgeRequest::write(QString note, int page, QString markdown, WriteMode mode)
{
    BridgeRequest r;
    r.kind = Kind::write;
    r.note = std::move(note);
    r.page = page;
    r.markdown = std::move(markdown);
    r.mode = mode;
    return r;
}

BridgeRequest BridgeRequest::draw(QString note, int page, QString dsl, std::optional<QString> after)
{
    BridgeRequest r;
    r.kind = Kind::draw;
    r.note = std::move(note);
    r.page = page;
    r.dsl = std::move(dsl);
    r.after = std::move(after);
    return r;
}

BridgeRequest BridgeRequest::editDrawing(QString note, QString drawing, QString dsl)
{
    BridgeRequest r;
    r.kind = Kind::editDrawing;
    r.note = std::move(note);
    r.drawing = std::move(drawing);
    r.dsl = std::move(dsl);
    return r;
}

BridgeRequest BridgeRequest::deleteDrawing(QString note, QString drawing)
{
    BridgeRequest r;
    r.kind = Kind::deleteDrawing;
    r.note = std::move(note);
    r.drawing = std::move(drawing);
    return r;
}

BridgeRequest BridgeRequest::addPage(QString note)
{
    BridgeRequest r;
    r.kind = Kind::addPage;
    r.note = std::move(note);
    return r;
}

BridgeRequest BridgeRequest::importDocument(QString path)
{
    BridgeRequest r;
    r.kind = Kind::importDocument;
    r.path = std::move(path);
    return r;
}

BridgeRequest BridgeRequest::listImports()
{
    BridgeRequest r;
    r.kind = Kind::listImports;
    return r;
}

BridgeRequest BridgeRequest::readChunk(QString importID, int chunk)
{
    BridgeRequest r;
    r.kind = Kind::readChunk;
    r.importID = std::move(importID);
    r.chunk = chunk;
    return r;
}

BridgeRequest BridgeRequest::savePoints(QString importID, int chunk, QList<StudyPoint> points)
{
    BridgeRequest r;
    r.kind = Kind::savePoints;
    r.importID = std::move(importID);
    r.chunk = chunk;
    r.points = std::move(points);
    return r;
}

BridgeRequest BridgeRequest::getPoints(std::optional<QString> importID)
{
    BridgeRequest r;
    r.kind = Kind::getPoints;
    r.importID = std::move(importID);
    return r;
}

BridgeRequest BridgeRequest::buildStudyPlan(QStringList importIDs, StudyPlan plan)
{
    BridgeRequest r;
    r.kind = Kind::buildStudyPlan;
    r.importIDs = std::move(importIDs);
    r.plan = std::move(plan);
    return r;
}

BridgeRequest BridgeRequest::createFlashcards(std::optional<QString> note, std::optional<int> page, QString title,
                                              QList<Flashcard> cards)
{
    BridgeRequest r;
    r.kind = Kind::createFlashcards;
    r.note = std::move(note);
    r.page = page;
    r.title = std::move(title);
    r.cards = std::move(cards);
    return r;
}

QString BridgeRequest::caseName() const
{
    for (const auto &k : kKindNames) {
        if (k.kind == kind)
            return QLatin1String(k.name);
    }
    return {};
}

QJsonObject BridgeRequest::toJson() const
{
    QJsonObject p;
    switch (kind) {
    case Kind::ping:
    case Kind::listNotes:
    case Kind::listImports:
        break;
    case Kind::readNote:
        put(p, "note", note);
        put(p, "page", page);
        break;
    case Kind::createNote:
        p.insert("title", title);
        put(p, "markdown", markdown);
        put(p, "folder", folder);
        break;
    case Kind::write:
        put(p, "note", note);
        put(p, "page", page);
        put(p, "markdown", markdown);
        p.insert("mode", writeModeName(mode));
        break;
    case Kind::draw:
        put(p, "note", note);
        put(p, "page", page);
        p.insert("dsl", dsl);
        put(p, "after", after);
        break;
    case Kind::editDrawing:
        put(p, "note", note);
        p.insert("drawing", drawing);
        p.insert("dsl", dsl);
        break;
    case Kind::deleteDrawing:
        put(p, "note", note);
        p.insert("drawing", drawing);
        break;
    case Kind::addPage:
        put(p, "note", note);
        break;
    case Kind::importDocument:
        p.insert("path", path);
        break;
    case Kind::readChunk:
        put(p, "importID", importID);
        p.insert("chunk", chunk);
        break;
    case Kind::savePoints: {
        put(p, "importID", importID);
        p.insert("chunk", chunk);
        QJsonArray array;
        for (const auto &point : points)
            array.append(point.toJson());
        p.insert("points", array);
        break;
    }
    case Kind::getPoints:
        put(p, "importID", importID);
        break;
    case Kind::buildStudyPlan:
        p.insert("importIDs", QJsonArray::fromStringList(importIDs));
        p.insert("plan", plan.toJson());
        break;
    case Kind::createFlashcards: {
        put(p, "note", note);
        put(p, "page", page);
        p.insert("title", title);
        QJsonArray array;
        for (const auto &card : cards)
            array.append(card.toJson());
        p.insert("cards", array);
        break;
    }
    }
    QJsonObject o;
    o.insert(caseName(), p);
    return o;
}

BridgeRequest BridgeRequest::fromJson(const QJsonObject &object)
{
    if (object.size() != 1)
        throw DecodingError{QStringLiteral("expected exactly one request case")};
    const QString name = object.keys().first();
    std::optional<Kind> found;
    for (const auto &k : kKindNames) {
        if (name == QLatin1String(k.name))
            found = k.kind;
    }
    if (!found)
        throw DecodingError{QStringLiteral("unknown request '%1'").arg(name)};
    const QJsonObject p = json::objectValue(object.value(name), name);

    BridgeRequest r;
    r.kind = *found;
    switch (r.kind) {
    case Kind::ping:
    case Kind::listNotes:
    case Kind::listImports:
        break;
    case Kind::readNote:
        r.note = json::requiredString(p, "note");
        r.page = json::optionalInt(p, "page");
        break;
    case Kind::createNote:
        r.title = json::requiredString(p, "title");
        r.markdown = json::optionalString(p, "markdown");
        r.folder = json::optionalString(p, "folder");
        break;
    case Kind::write: {
        r.note = json::requiredString(p, "note");
        r.page = json::intValue(p.value("page"), "page");
        r.markdown = json::requiredString(p, "markdown");
        const auto mode = writeModeFromName(json::requiredString(p, "mode"));
        if (!mode)
            throw DecodingError{QStringLiteral("invalid mode")};
        r.mode = *mode;
        break;
    }
    case Kind::draw:
        r.note = json::requiredString(p, "note");
        r.page = json::intValue(p.value("page"), "page");
        r.dsl = json::requiredString(p, "dsl");
        r.after = json::optionalString(p, "after");
        break;
    case Kind::editDrawing:
        r.note = json::requiredString(p, "note");
        r.drawing = json::requiredString(p, "drawing");
        r.dsl = json::requiredString(p, "dsl");
        break;
    case Kind::deleteDrawing:
        r.note = json::requiredString(p, "note");
        r.drawing = json::requiredString(p, "drawing");
        break;
    case Kind::addPage:
        r.note = json::requiredString(p, "note");
        break;
    case Kind::importDocument:
        r.path = json::requiredString(p, "path");
        break;
    case Kind::readChunk:
        r.importID = json::requiredString(p, "importID");
        r.chunk = json::intValue(p.value("chunk"), "chunk");
        break;
    case Kind::savePoints:
        r.importID = json::requiredString(p, "importID");
        r.chunk = json::intValue(p.value("chunk"), "chunk");
        for (const QJsonValue &v : json::requiredArray(p, "points"))
            r.points.append(StudyPoint::fromJson(json::objectValue(v, "points")));
        break;
    case Kind::getPoints:
        r.importID = json::optionalString(p, "importID");
        break;
    case Kind::buildStudyPlan:
        for (const QJsonValue &v : json::requiredArray(p, "importIDs")) {
            if (!v.isString())
                throw DecodingError{QStringLiteral("invalid string in 'importIDs'")};
            r.importIDs.append(v.toString());
        }
        r.plan = StudyPlan::fromJson(json::objectValue(p.value("plan"), "plan"));
        break;
    case Kind::createFlashcards:
        r.note = json::optionalString(p, "note");
        r.page = json::optionalInt(p, "page");
        r.title = json::requiredString(p, "title");
        for (const QJsonValue &v : json::requiredArray(p, "cards"))
            r.cards.append(Flashcard::fromJson(json::objectValue(v, "cards")));
        break;
    }
    return r;
}

QString BridgeRequest::description() const
{
    return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}

// MARK: BridgeResponse

QJsonObject BridgeResponse::toJson() const
{
    QJsonObject payload;
    payload.insert("_0", text);
    QJsonObject o;
    o.insert(isOk ? "ok" : "failure", payload);
    return o;
}

BridgeResponse BridgeResponse::fromJson(const QJsonObject &object)
{
    if (object.size() != 1)
        throw DecodingError{QStringLiteral("expected exactly one response case")};
    const QString name = object.keys().first();
    if (name != QLatin1String("ok") && name != QLatin1String("failure"))
        throw DecodingError{QStringLiteral("unknown response '%1'").arg(name)};
    const QJsonObject payload = json::objectValue(object.value(name), name);
    return {name == QLatin1String("ok"), json::requiredString(payload, "_0")};
}

// MARK: BridgeError

BridgeError BridgeError::appNotRunning() { return {}; }

BridgeError BridgeError::timedOut(double seconds)
{
    BridgeError e;
    e.kind = Kind::timedOut;
    e.seconds = seconds;
    return e;
}

BridgeError BridgeError::alreadyRunning(QString name)
{
    BridgeError e;
    e.kind = Kind::alreadyRunning;
    e.detail = std::move(name);
    return e;
}

BridgeError BridgeError::pathTooLong(QString name)
{
    BridgeError e;
    e.kind = Kind::pathTooLong;
    e.detail = std::move(name);
    return e;
}

BridgeError BridgeError::launchFailed(QString reason)
{
    BridgeError e;
    e.kind = Kind::launchFailed;
    e.detail = std::move(reason);
    return e;
}

BridgeError BridgeError::badReply(QString reason)
{
    BridgeError e;
    e.kind = Kind::badReply;
    e.detail = std::move(reason);
    return e;
}

BridgeError BridgeError::system(QString call, QString reason)
{
    BridgeError e;
    e.kind = Kind::system;
    e.detail = std::move(call);
    e.reason = std::move(reason);
    return e;
}

QString BridgeError::description() const
{
    switch (kind) {
    case Kind::appNotRunning:
        return QStringLiteral("Whiteprint isn't running");
    case Kind::timedOut:
        return QStringLiteral("Whiteprint didn't reply within %1 s").arg(static_cast<long long>(seconds));
    case Kind::alreadyRunning:
        return QStringLiteral("another Whiteprint is already listening on %1").arg(detail);
    case Kind::pathTooLong:
        return QStringLiteral("pipe name is too long (max 200 characters): %1").arg(detail);
    case Kind::launchFailed:
        return QStringLiteral("couldn't start Whiteprint: %1").arg(detail);
    case Kind::badReply:
        return QStringLiteral("bad reply from Whiteprint: %1").arg(detail);
    case Kind::system:
        return QStringLiteral("%1 failed: %2").arg(detail, reason);
    }
    return {};
}

const char *BridgeError::what() const noexcept
{
    whatCache_ = description().toStdString();
    return whatCache_.c_str();
}

// MARK: BridgePaths

QString BridgePaths::supportDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    const QString dir = base + QStringLiteral("/Whiteprint");
    QDir().mkpath(dir);
    return QDir::toNativeSeparators(dir);
}

QString BridgePaths::socketName()
{
    const QString env = qEnvironmentVariable("WHITEPRINT_SOCKET");
    if (!env.isEmpty()) {
        // A full pipe path is used as given; anything else must not contain separators.
        if (env.startsWith(QStringLiteral("\\\\.\\pipe\\")))
            return env.mid(9);
        QString name = env;
        name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
        return name;
    }
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = QStringLiteral("user");
    user.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\s]")), QStringLiteral("_"));
    return QStringLiteral("whiteprint-") + user;
}

QString BridgePaths::helperExecutable()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + QStringLiteral("/whiteprint-mcp.exe"));
}

QString ToolCallError::description() const
{
    switch (kind) {
    case Kind::unknownTool:
        return QStringLiteral("unknown tool: %1").arg(detail);
    case Kind::invalidArguments:
        return QStringLiteral("Invalid arguments: %1").arg(detail);
    }
    return {};
}

} // namespace wp
