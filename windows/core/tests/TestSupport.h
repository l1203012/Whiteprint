#pragma once
// Shared helpers for the core tests (header only, so it isn't built as a test itself).
#include <QString>
#include <QtTest>
#include <optional>

namespace wp::testing {

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

inline QString U(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

} // namespace wp::testing

/// Equality check that works for any type with operator==.
#define WP_EQ(actual, expected) QVERIFY((actual) == (expected))
