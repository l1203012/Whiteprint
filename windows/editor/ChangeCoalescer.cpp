#include "editor/ChangeCoalescer.h"

namespace wp {

ChangeCoalescer::ChangeCoalescer(int delayMs, QObject *parent) : QObject(parent), m_delay(delayMs)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(delayMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { flush(); });
}

void ChangeCoalescer::schedule(std::function<void()> action)
{
    const bool wasPending = isPending();
    m_action = std::move(action);
    if (!wasPending)
        m_timer.start();
}

void ChangeCoalescer::flush()
{
    m_timer.stop();
    auto pending = std::move(m_action);
    m_action = nullptr;
    if (pending)
        pending();
}

} // namespace wp
