#include "Timer.h"

namespace canvas {

Timer::Timer()
{
    reset();
}

void Timer::reset()
{
    m_base = Clock::now();
    m_pauseStart = m_base;
    m_pausedSeconds = 0.0;
    m_paused = false;
}

void Timer::pause()
{
    if (m_paused) return;
    m_pauseStart = Clock::now();
    m_paused = true;
}

void Timer::resume()
{
    if (!m_paused) return;
    m_pausedSeconds += std::chrono::duration<double>(Clock::now() - m_pauseStart).count();
    m_paused = false;
}

double Timer::seconds() const
{
    const Clock::time_point now = Clock::now();
    double elapsed = std::chrono::duration<double>(now - m_base).count() - m_pausedSeconds;
    if (m_paused) {
        elapsed -= std::chrono::duration<double>(now - m_pauseStart).count();
    }
    return elapsed > 0.0 ? elapsed : 0.0;
}

} // namespace canvas
