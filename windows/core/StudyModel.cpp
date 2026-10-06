#include "core/StudyModel.h"

#include "core/JsonCoding.h"

namespace wp {

QString importanceName(Importance importance)
{
    switch (importance) {
    case Importance::must: return QStringLiteral("must");
    case Importance::good: return QStringLiteral("good");
    case Importance::skip: return QStringLiteral("skip");
    }
    return {};
}

std::optional<Importance> importanceFromName(const QString &name)
{
    for (Importance i : allImportances()) {
        if (importanceName(i) == name)
            return i;
    }
    return std::nullopt;
}

QList<Importance> allImportances()
{
    return {Importance::must, Importance::good, Importance::skip};
}

namespace {

void put(QJsonObject &o, const char *key, const std::optional<QString> &value)
{
    if (value)
        o.insert(QLatin1String(key), *value);
}

} // namespace

QJsonObject StudyPoint::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("text"), text);
    o.insert(QStringLiteral("importance"), importanceName(importance));
    o.insert(QStringLiteral("ref"), ref);
    put(o, "topic", topic);
    return o;
}

StudyPoint StudyPoint::fromJson(const QJsonObject &object)
{
    const QString name = json::requiredString(object, QStringLiteral("importance"));
    const auto importance = importanceFromName(name);
    if (!importance)
        throw DecodingError{QStringLiteral("invalid importance '%1'").arg(name)};
    return StudyPoint(json::requiredString(object, QStringLiteral("text")), *importance,
                      json::requiredString(object, QStringLiteral("ref")),
                      json::optionalString(object, QStringLiteral("topic")));
}

QJsonObject StudyTask::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("text"), text);
    put(o, "due", due);
    put(o, "ref", ref);
    return o;
}

StudyTask StudyTask::fromJson(const QJsonObject &object)
{
    return StudyTask(json::requiredString(object, QStringLiteral("text")),
                     json::optionalString(object, QStringLiteral("due")),
                     json::optionalString(object, QStringLiteral("ref")));
}

QJsonObject StudyModule::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("title"), title);
    if (minutes)
        o.insert(QStringLiteral("minutes"), *minutes);
    QJsonArray array;
    for (const StudyPoint &p : points)
        array.append(p.toJson());
    o.insert(QStringLiteral("points"), array);
    return o;
}

StudyModule StudyModule::fromJson(const QJsonObject &object)
{
    QList<StudyPoint> points;
    for (const QJsonValue &v : json::requiredArray(object, QStringLiteral("points")))
        points.append(StudyPoint::fromJson(json::objectValue(v, QStringLiteral("points"))));
    return StudyModule(json::requiredString(object, QStringLiteral("title")),
                       json::optionalInt(object, QStringLiteral("minutes")), points);
}

QJsonObject StudyPlan::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("title"), title);
    o.insert(QStringLiteral("overview"), overview);
    QJsonArray m;
    for (const StudyModule &module : modules)
        m.append(module.toJson());
    o.insert(QStringLiteral("modules"), m);
    QJsonArray t;
    for (const StudyTask &task : tasks)
        t.append(task.toJson());
    o.insert(QStringLiteral("tasks"), t);
    QJsonArray d;
    for (const QString &diagram : diagrams)
        d.append(diagram);
    o.insert(QStringLiteral("diagrams"), d);
    QJsonArray f;
    for (const Flashcard &card : flashcards)
        f.append(card.toJson());
    o.insert(QStringLiteral("flashcards"), f);
    return o;
}

StudyPlan StudyPlan::fromJson(const QJsonObject &object)
{
    StudyPlan plan;
    plan.title = json::requiredString(object, QStringLiteral("title"));
    plan.overview = json::requiredString(object, QStringLiteral("overview"));
    for (const QJsonValue &v : json::requiredArray(object, QStringLiteral("modules")))
        plan.modules.append(StudyModule::fromJson(json::objectValue(v, QStringLiteral("modules"))));
    for (const QJsonValue &v : json::optionalArray(object, QStringLiteral("tasks")))
        plan.tasks.append(StudyTask::fromJson(json::objectValue(v, QStringLiteral("tasks"))));
    for (const QJsonValue &v : json::optionalArray(object, QStringLiteral("diagrams"))) {
        if (!v.isString())
            throw DecodingError{QStringLiteral("invalid string in 'diagrams'")};
        plan.diagrams.append(v.toString());
    }
    for (const QJsonValue &v : json::optionalArray(object, QStringLiteral("flashcards")))
        plan.flashcards.append(Flashcard::fromJson(json::objectValue(v, QStringLiteral("flashcards"))));
    return plan;
}

} // namespace wp
