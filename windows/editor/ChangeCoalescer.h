#pragma once
#include <QObject>
#include <QTimer>
#include <functional>

namespace wp {

/// Runs the latest scheduled action at most once per `delay` (milliseconds).
class ChangeCoalescer : public QObject
{
public:
    explicit ChangeCoalescer(int delayMs, QObject *parent = nullptr);

    int delay() const { return m_delay; }
    bool isPending() const { return bool(m_action); }

    void schedule(std::function<void()> action);
    /// Runs the pending action now, if any.
    void flush();

private:
    int m_delay;
    std::function<void()> m_action;
    QTimer m_timer;
};

} // namespace wp
