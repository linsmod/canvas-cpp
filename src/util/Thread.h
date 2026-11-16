#pragma once

#include <atomic>
#include <pthread.h>

namespace canvas {

class Thread {
public:
    explicit Thread(bool joinable = true);
    virtual ~Thread();

    void start();
    void requestStop();
    bool isStopRequested() const { return m_stopRequested.load(std::memory_order_acquire); }

    pthread_t getThreadId() const { return m_thread; }
    bool isRunning() const { return m_started; }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

protected:
    virtual void run() = 0;

private:
    static void* threadEntry(void* self);

    pthread_t m_thread{};
    bool m_started = false;
    bool m_joinable = true;
    std::atomic<bool> m_stopRequested{false};
};

} // namespace canvas
