#include "CondVar.h"

#ifndef _WIN32
#include <ctime>
#endif

namespace canvas {

CondVar::CondVar()
{
#ifdef _WIN32
    InitializeConditionVariable(&m_cond);
#else
    pthread_cond_init(&m_cond, nullptr);
#endif
}

CondVar::~CondVar()
{
#ifndef _WIN32
    pthread_cond_destroy(&m_cond);
#endif
}

void CondVar::wait(Mutex& mutex)
{
#ifdef _WIN32
    SleepConditionVariableSRW(&m_cond, mutex.nativeHandle(), INFINITE, 0);
#else
    pthread_cond_wait(&m_cond, mutex.nativeHandle());
#endif
}

bool CondVar::waitFor(Mutex& mutex, int timeoutMs)
{
#ifdef _WIN32
    return SleepConditionVariableSRW(&m_cond, mutex.nativeHandle(),
                                     static_cast<DWORD>(timeoutMs < 0 ? 0 : timeoutMs), 0) != 0;
#else
    timespec deadline{};
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeoutMs / 1000;
    deadline.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&m_cond, mutex.nativeHandle(), &deadline) == 0;
#endif
}

void CondVar::signal()
{
#ifdef _WIN32
    WakeConditionVariable(&m_cond);
#else
    pthread_cond_signal(&m_cond);
#endif
}

void CondVar::broadcast()
{
#ifdef _WIN32
    WakeAllConditionVariable(&m_cond);
#else
    pthread_cond_broadcast(&m_cond);
#endif
}

} // namespace canvas
