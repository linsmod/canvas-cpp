#pragma once

#include <chrono>

namespace canvas {

class Timer {
public:
    Timer();

    void reset();
    double seconds() const;
    void pause();
    void resume();

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point m_base;
    Clock::time_point m_pauseStart;
    double m_pausedSeconds = 0.0;
    bool m_paused = false;
};

} // namespace canvas
