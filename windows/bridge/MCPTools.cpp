#include "bridge/MCPTools.h"
#include "core/JsonCoding.h"

#include <QJsonArray>

namespace wp {

using P = ToolSchema::Property;
using S = ToolSchema;

// MARK: ToolArguments

QString ToolArguments::string(const QString &key) const
{
    const QJsonValue v = values.value(key);
    if (!v.isString())
        throw ArgumentError(key, QStringLiteral("required"));
    return v.toString();
}

std::optional<QString> ToolArguments::optionalString(const QString &key) const
{
    const QJsonValue v = values.value(key);
    if (!v.isString())
        return std::nullopt;
    return v.toString();
}

int ToolArguments::integer(const QString &key) const
{
    const QJsonValue v = values.value(key);
    if (!v.isDouble())
        throw ArgumentError(key, QStringLiteral("required"));
    return static_cast<int>(v.toInteger());
}

std::optional<int> ToolArguments::optionalInt(const QString &key) const
{
    const QJsonValue v = values.value(key);
    if (!v.isDouble())
        return std::nullopt;
    return static_cast<int>(v.toInteger());
}

std::optional<QString> ToolArguments::folder(const QString &key) const
{
    const auto raw = optionalString(key);
    if (!raw)
        return std::nullopt;
    auto isTrim = [](QChar c) { return c == QLatin1Char('/') || c == QLatin1Char(' ') || c == QLatin1Char('\t'); };
    int start = 0;
    int end = raw->size();
    while (start < end && isTrim(raw->at(start)))
        ++start;
    while (end > start && isTrim(raw->at(end - 1)))
        --end;
    const QString path = raw->mid(start, end - start);
    const bool escapes = path.split(QLatin1Char('/'), Qt::SkipEmptyParts).contains(QStringLiteral(".."));
    if (raw->startsWith(QLatin1Char('/')) || raw->startsWith(QLatin1Char('~')) || escapes)
        throw ArgumentError(key, QStringLiteral("use a path inside the notes folder, e.g. Courses/Networks"));
    if (path.isEmpty())
        return std::nullopt;
    return path;
}

QList<StudyPoint> ToolArguments::points(const QString &key) const
{
    try {
        QList<StudyPoint> result;
        for (const QJsonValue &v : values.value(key).toArray())
            result.append(StudyPoint::fromJson(json::objectValue(v, key)));
        return result;
    } catch (const DecodingError &) {
        throw ArgumentError(key, QStringLiteral("invalid Array<StudyPoint>"));
    }
}

QList<Flashcard> ToolArguments::cards(const QString &key) const
{
    try {
        QList<Flashcard> result;
        for (const QJsonValue &v : values.value(key).toArray())
            result.append(Flashcard::fromJson(json::objectValue(v, key)));
        return result;
    } catch (const DecodingError &) {
        throw ArgumentError(key, QStringLiteral("invalid Array<Flashcard>"));
    }
}

QStringList ToolArguments::strings(const QString &key) const
{
    QStringList result;
    for (const QJsonValue &v : values.value(key).toArray())
        result.append(v.toString());
    return result;
}

StudyPlan ToolArguments::plan(const QString &key) const
{
    try {
        return StudyPlan::fromJson(json::objectValue(values.value(key), key));
    } catch (const DecodingError &) {
        throw ArgumentError(key, QStringLiteral("invalid StudyPlan"));
    }
}

// MARK: MCPTool

AgentTool MCPTool::agentTool() const
{
    QMap<QString, bool> hints;
    switch (effect) {
    case Effect::readOnly:
        hints.insert(QStringLiteral("readOnlyHint"), true);
        break;
    case Effect::additive:
        hints.insert(QStringLiteral("destructiveHint"), false);
        break;
    case Effect::destructive:
        hints.insert(QStringLiteral("destructiveHint"), true);
        break;
    }
    return {name, description, S::object(arguments).json(), hints};
}

BridgeRequest MCPTool::makeRequest(const QJsonObject &args) const
{
    const QJsonValue valid = S::object(arguments).validate(args);
    return request(ToolArguments{valid.toObject()});
}

namespace {

S pointSchema()
{
    QStringList importance;
    for (Importance i : allImportances())
        importance.append(importanceName(i));
    return S::object({
        P::required("text", S::string()),
        P::required("importance", S::oneOf(importance)),
        P::required("ref", S::string()),
        P::optional("topic", S::string()),
    });
}

S cardSchema()
{
    return S::object({
        P::required("question", S::string()),
        P::required("answer", S::string()),
        P::optional("ref", S::string()),
    });
}

S planSchema()
{
    return S::object({
        P::required("title", S::string()),
        P::required("overview", S::string()),
        P::required("modules", S::array(S::object({
                                   P::required("title", S::string()),
                                   P::optional("minutes", S::integer()),
                                   P::required("points", S::array(pointSchema())),
                               }))),
        P::optional("tasks", S::array(S::object({
                                 P::required("text", S::string()),
                                 P::optional("due", S::string()),
                                 P::optional("ref", S::string()),
                             }))),
        P::optional("diagrams", S::array(S::string())),
        P::optional("flashcards", S::array(cardSchema())),
    });
}

QList<MCPTool> buildTools()
{
    using E = MCPTool::Effect;
    using A = const ToolArguments &;
    QList<MCPTool> t;
    t.append({"list_notes", "List notes: id, title, pages.", {}, E::readOnly,
              [](A) { return BridgeRequest::listNotes(); }});
    t.append({"read_note", "Read a note's source, or one page of it.",
              {P::required("note", S::string()), P::optional("page", S::integer())}, E::readOnly,
              [](A a) { return BridgeRequest::readNote(a.string("note"), a.optionalInt("page")); }});
    t.append({"create_note", "Create a note, optionally in a folder (e.g. Courses/Networks). Returns its id.",
              {P::required("title", S::string()), P::optional("markdown", S::string()),
               P::optional("folder", S::string())},
              E::additive, [](A a) {
                  // Evaluation order matters for which error wins: title, then folder.
                  const QString title = a.string("title");
                  return BridgeRequest::createNote(title, a.optionalString("markdown"), a.folder("folder"));
              }});
    t.append({"write", "Append to or replace a page's markdown.",
              {P::required("note", S::string()), P::required("page", S::integer()),
               P::required("markdown", S::string()), P::required("mode", S::oneOf({"append", "replace"}))},
              E::destructive, [](A a) {
                  return BridgeRequest::write(a.string("note"), a.integer("page"), a.string("markdown"),
                                              writeModeFromName(a.string("mode")).value_or(WriteMode::append));
              }});
    t.append({"draw", "Add a drawing to a page. Language: resource whiteprint://dsl. Returns its id.",
              {P::required("note", S::string()), P::required("page", S::integer()), P::required("dsl", S::string()),
               P::optional("after", S::string())},
              E::additive, [](A a) {
                  return BridgeRequest::draw(a.string("note"), a.integer("page"), a.string("dsl"),
                                             a.optionalString("after"));
              }});
    t.append({"edit_drawing", "Replace a drawing's source.",
              {P::required("note", S::string()), P::required("drawing", S::string()), P::required("dsl", S::string())},
              E::destructive,
              [](A a) { return BridgeRequest::editDrawing(a.string("note"), a.string("drawing"), a.string("dsl")); }});
    t.append({"delete_drawing", "Delete a drawing.",
              {P::required("note", S::string()), P::required("drawing", S::string())}, E::destructive,
              [](A a) { return BridgeRequest::deleteDrawing(a.string("note"), a.string("drawing")); }});
    t.append({"add_page", "Add a page at the end. Returns its number.", {P::required("note", S::string())},
              E::additive, [](A a) { return BridgeRequest::addPage(a.string("note")); }});
    t.append({"import_document", "Import a PDF, Word or PowerPoint file for study. Returns its id.",
              {P::required("path", S::string())}, E::additive,
              [](A a) { return BridgeRequest::importDocument(a.string("path")); }});
    t.append({"list_imports", "List imports: id, name, pages, chunks, status.", {}, E::readOnly,
              [](A) { return BridgeRequest::listImports(); }});
    t.append({"read_chunk", "Read one chunk of an import, lines tagged with source refs.",
              {P::required("import", S::string()), P::required("chunk", S::integer())}, E::readOnly,
              [](A a) { return BridgeRequest::readChunk(a.string("import"), a.integer("chunk")); }});
    t.append({"save_points", "Save the key points found in a chunk.",
              {P::required("import", S::string()), P::required("chunk", S::integer()),
               P::required("points", S::array(pointSchema()))},
              E::additive, [](A a) {
                  return BridgeRequest::savePoints(a.string("import"), a.integer("chunk"), a.points("points"));
              }});
    t.append({"get_points", "List saved points, for all imports or one.", {P::optional("import", S::string())},
              E::readOnly, [](A a) { return BridgeRequest::getPoints(a.optionalString("import")); }});
    t.append({"build_study_plan", "Write the final study plan as a new note. Returns its id.",
              {P::required("imports", S::array(S::string())), P::required("plan", planSchema())}, E::additive,
              [](A a) {
                  const QStringList imports = a.strings("imports");
                  return BridgeRequest::buildStudyPlan(imports, a.plan("plan"));
              }});
    t.append({"create_flashcards",
              "Add a flashcard deck to a note's page (default: last), or to a new note. Returns ids.",
              {P::optional("note", S::string()), P::optional("page", S::integer()), P::required("title", S::string()),
               P::required("cards", S::array(cardSchema()))},
              E::additive, [](A a) {
                  const QList<Flashcard> cards = a.cards("cards");
                  if (cards.isEmpty())
                      throw ArgumentError(QStringLiteral("cards"), QStringLiteral("add at least one card"));
                  return BridgeRequest::createFlashcards(a.optionalString("note"), a.optionalInt("page"),
                                                         a.string("title"), cards);
              }});
    return t;
}

} // namespace

const QList<MCPTool> &MCPTool::all()
{
    static const QList<MCPTool> tools = buildTools();
    return tools;
}

const MCPTool *MCPTool::named(const QString &name)
{
    for (const MCPTool &tool : all()) {
        if (tool.name == name)
            return &tool;
    }
    return nullptr;
}

// MARK: MCPToolCatalog

namespace MCPToolCatalog {

QList<AgentTool> tools()
{
    QList<AgentTool> result;
    for (const MCPTool &tool : MCPTool::all())
        result.append(tool.agentTool());
    return result;
}

BridgeRequest request(const QString &forTool, const QJsonObject &arguments)
{
    const MCPTool *tool = MCPTool::named(forTool);
    if (!tool)
        throw ToolCallError::unknownTool(forTool);
    try {
        return tool->makeRequest(arguments);
    } catch (const ArgumentError &e) {
        throw ToolCallError::invalidArguments(e.description());
    }
}

} // namespace MCPToolCatalog

} // namespace wp
