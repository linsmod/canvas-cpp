#pragma once

#include <atomic>

namespace canvas {

inline std::atomic<int>& errorCountStorage()
{
    static std::atomic<int> count{0};
    return count;
}

inline void incrementErrorCount()
{
    errorCountStorage().fetch_add(1, std::memory_order_relaxed);
}

inline int getAndResetErrorCount()
{
    return errorCountStorage().exchange(0, std::memory_order_relaxed);
}

inline std::atomic<int>& g_jsErrorCount = errorCountStorage();

} // namespace canvas
