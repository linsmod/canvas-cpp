#pragma once

#include <string>

namespace canvas {

class Perf {
public:
    static void reset();
    static void dump();
    static void add(const char* name, double seconds);
};

class PerfScope {
public:
    explicit PerfScope(const char* name);
    ~PerfScope();

    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    const char* m_name;
    double m_startSeconds;
};

} // namespace canvas

#define PERF_CONCAT_IMPL(a, b) a##b
#define PERF_CONCAT(a, b) PERF_CONCAT_IMPL(a, b)
#define PERF_SCOPE(name) ::canvas::PerfScope PERF_CONCAT(_perf_scope_, __LINE__)(name)
