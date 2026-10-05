#pragma once
#include "core/CardDeck.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <optional>

namespace wp {

// Shared types for the study agent. Claude sends these through the MCP
// study tools; the study module stores them and renders the final note.
// Each type has `toJson()` and `fromJson()` matching Swift's Codable output (nil fields are omitted);
// `fromJson` throws DecodingError (core/JsonCoding.h) when required fields are missing or mistyped.

enum class Importance {
    /// Core concepts, likely exam material.
    must,
    /// Supporting detail.
    good,
    /// Examples, history, repetition, filler.
    skip,
};

/// `must`, `good` or `skip` (Swift's rawValue).
QString importanceName(Importance importance);
std::optional<Importance> importanceFromName(const QString &name);
/// All cases in declaration order (Swift's CaseIterable).
QList<Importance> allImportances();

/// One finding from a chunk of source material.
struct StudyPoint {
    QString text;
    Importance importance = Importance::good;
    /// Where it came from, e.g. `Lecture3.pptx · slide 14`.
    QString ref;
    /// Optional topic hint used when grouping points into modules.
    std::optional<QString> topic;

    StudyPoint() = default;
    StudyPoint(QString text, Importance importance, QString ref, std::optional<QString> topic = std::nullopt)
        : text(std::move(text)), importance(importance), ref(std::move(ref)), topic(std::move(topic))
    {
    }
    bool operator==(const StudyPoint &) const = default;

    QJsonObject toJson() const;
    static StudyPoint fromJson(const QJsonObject &object);
};

/// Something to do: an assignment, exercise, reading or deadline.
struct StudyTask {
    QString text;
    /// Free-form due date as found in the material, e.g. `2026-01-15` or `week 6`.
    std::optional<QString> due;
    std::optional<QString> ref;

    StudyTask() = default;
    StudyTask(QString text, std::optional<QString> due = std::nullopt, std::optional<QString> ref = std::nullopt)
        : text(std::move(text)), due(std::move(due)), ref(std::move(ref))
    {
    }
    bool operator==(const StudyTask &) const = default;

    QJsonObject toJson() const;
    static StudyTask fromJson(const QJsonObject &object);
};

struct StudyModule {
    QString title;
    /// Estimated study time.
    std::optional<int> minutes;
    QList<StudyPoint> points;

    StudyModule() = default;
    StudyModule(QString title, std::optional<int> minutes, QList<StudyPoint> points)
        : title(std::move(title)), minutes(minutes), points(std::move(points))
    {
    }
    bool operator==(const StudyModule &) const = default;

    QJsonObject toJson() const;
    static StudyModule fromJson(const QJsonObject &object);
};

/// The finished plan Claude sends to `build_study_plan`.
struct StudyPlan {
    QString title;
    /// 5-10 lines on what the material covers.
    QString overview;
    /// In the order they should be studied.
    QList<StudyModule> modules;
    QList<StudyTask> tasks;
    /// Optional diagrams in the drawing language.
    QStringList diagrams;
    /// Question/answer cards for practising the must-know points.
    QList<Flashcard> flashcards;

    StudyPlan() = default;
    StudyPlan(QString title, QString overview, QList<StudyModule> modules, QList<StudyTask> tasks = {},
              QStringList diagrams = {}, QList<Flashcard> flashcards = {})
        : title(std::move(title)), overview(std::move(overview)), modules(std::move(modules)),
          tasks(std::move(tasks)), diagrams(std::move(diagrams)), flashcards(std::move(flashcards))
    {
    }
    bool operator==(const StudyPlan &) const = default;

    QJsonObject toJson() const;
    /// `tasks`, `diagrams` and `flashcards` may be left out.
    static StudyPlan fromJson(const QJsonObject &object);
};

} // namespace wp
