#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace canvas {

class Mutex {
public:
    Mutex();
    ~Mutex();

    void lock();
    void unlock();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

#ifdef _WIN32
    SRWLOCK* nativeHandle() { return &m_mutex; }
#else
    pthread_mutex_t* nativeHandle() { return &m_mutex; }
#endif

private:
#ifdef _WIN32
    SRWLOCK m_mutex;
#else
    pthread_mutex_t m_mutex;
#endif
};

} // namespace canvas
