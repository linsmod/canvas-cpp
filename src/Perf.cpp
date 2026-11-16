#include "Perf.h"
#include "Log.h"

#include <chrono>
#include <cstdint>
#include <map>

namespace canvas {
namespace {

struct Entry {
    double totalSeconds = 0.0;
    std::uint64_t calls = 0;
};

thread_local std::map<std::string, Entry> t_entries;

double nowSeconds()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

} // namespace

void Perf::reset()
{
    t_entries.clear();
}

void Perf::add(const char* name, double seconds)
{
    if (!name || seconds < 0.0) return;
    Entry& entry = t_entries[name];
    entry.totalSeconds += seconds;
    ++entry.calls;
}

void Perf::dump()
{
    for (const auto& item : t_entries) {
        const Entry& entry = item.second;
        LOGI("[Perf] %-24s calls=%llu total=%.3fms avg=%.3fms",
             item.first.c_str(),
             static_cast<unsigned long long>(entry.calls),
             entry.totalSeconds * 1000.0,
             entry.totalSeconds * 1000.0 / static_cast<double>(entry.calls));
    }
}

PerfScope::PerfScope(const char* name)
    : m_name(name), m_startSeconds(nowSeconds())
{
}

PerfScope::~PerfScope()
{
    Perf::add(m_name, nowSeconds() - m_startSeconds);
}

} // namespace canvas
