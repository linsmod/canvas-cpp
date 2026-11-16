#pragma once

#include "Mutex.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace canvas {

class CondVar {
public:
    CondVar();
    ~CondVar();

    void wait(Mutex& mutex);
    bool waitFor(Mutex& mutex, int timeoutMs);
    void signal();
    void broadcast();

    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;

private:
#ifdef _WIN32
    CONDITION_VARIABLE m_cond;
#else
    pthread_cond_t m_cond;
#endif
};

} // namespace canvas
