#pragma once

#include "Mutex.h"

namespace canvas {

class ScopedLock {
public:
    explicit ScopedLock(Mutex& mutex) : m_mutex(mutex) { m_mutex.lock(); }
    ~ScopedLock() { m_mutex.unlock(); }

    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;

private:
    Mutex& m_mutex;
};

} // namespace canvas
