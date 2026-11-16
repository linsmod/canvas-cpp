#include "Thread.h"
#include "Log.h"

#include <cstdio>

namespace canvas {

Thread::Thread(bool joinable) : m_joinable(joinable)
{
}

Thread::~Thread()
{
    if (m_started && m_joinable) {
        pthread_join(m_thread, nullptr);
    }
}

void* Thread::threadEntry(void* self)
{
    static_cast<Thread*>(self)->run();
    return nullptr;
}

void Thread::start()
{
    if (m_started) return;

    int rc = pthread_create(&m_thread, nullptr, &Thread::threadEntry, this);
    if (rc != 0) {
        LOGE("[Thread] pthread_create failed: %d", rc);
        return;
    }
    m_started = true;
    if (!m_joinable) {
        pthread_detach(m_thread);
    }
}

void Thread::requestStop()
{
    m_stopRequested.store(true, std::memory_order_release);
}

} // namespace canvas
