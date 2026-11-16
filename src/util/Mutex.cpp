#include "Mutex.h"

namespace canvas {

Mutex::Mutex()
{
#ifdef _WIN32
    InitializeSRWLock(&m_mutex);
#else
    pthread_mutex_init(&m_mutex, nullptr);
#endif
}

Mutex::~Mutex()
{
#ifndef _WIN32
    pthread_mutex_destroy(&m_mutex);
#endif
}

void Mutex::lock()
{
#ifdef _WIN32
    AcquireSRWLockExclusive(&m_mutex);
#else
    pthread_mutex_lock(&m_mutex);
#endif
}

void Mutex::unlock()
{
#ifdef _WIN32
    ReleaseSRWLockExclusive(&m_mutex);
#else
    pthread_mutex_unlock(&m_mutex);
#endif
}

} // namespace canvas
