#pragma once
// Shared helpers for the bridge tests (header only, so it isn't built as a test itself).
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtTest>
#include <chrono>
#include <future>
#include <optional>

/// Like QVERIFY2 but usable in functions that return a value (records the failure, doesn't return).
#define CHECK2(cond, msg) QTest::qVerify((cond), #cond, (msg), __FILE__, __LINE__)

namespace wp::testing {

inline QString U(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

/// Runs `f`; returns the exception of type E it threw, if any.
template <class E, class F>
std::optional<E> caught(F &&f)
{
    try {
        f();
    } catch (const E &e) {
        return e;
    }
    return std::nullopt;
}

/// Runs blocking work on a background thread while this thread keeps running its event loop
/// (the server lives here). Rethrows what `body` throws.
template <class F>
auto background(F body)
{
    auto future = std::async(std::launch::async, std::move(body));
    while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
        QCoreApplication::processEvents();
    return future.get();
}

/// Parses any JSON value, including bare scalars and arrays.
inline QJsonValue parseValue(const QString &json)
{
    const QJsonDocument wrapped = QJsonDocument::fromJson("[" + json.toUtf8() + "]");
    return wrapped.array().at(0);
}

inline QJsonObject parseObject(const QString &json)
{
    return parseValue(json).toObject();
}

} // namespace wp::testing
