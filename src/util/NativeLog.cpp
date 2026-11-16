#include "NativeLog.h"
#include "ErrorCount.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace canvas {
namespace {

constexpr std::size_t kMaxRecentLines = 128;

std::mutex g_logMutex;
std::vector<std::string> g_recentLines;
LogLevel g_minLevel = LogLevel::Info;

const char* levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Info:    return "I";
    case LogLevel::Warning: return "W";
    case LogLevel::Error:   return "E";
    }
    return "?";
}

} // namespace

void setNativeLogMinLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_minLevel = level;
}

std::vector<std::string> recentLogLines(std::size_t maxLines)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (maxLines == 0 || maxLines > g_recentLines.size()) {
        return g_recentLines;
    }
    return std::vector<std::string>(g_recentLines.end() - maxLines, g_recentLines.end());
}

void clearRecentLog()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_recentLines.clear();
}

std::string NativeLog::pop()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_recentLines.empty()) return std::string();
    std::string first = g_recentLines.front();
    g_recentLines.erase(g_recentLines.begin());
    return first;
}

std::vector<std::string> NativeLog::recent(std::size_t maxLines)
{
    return recentLogLines(maxLines);
}

void nativeLog(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (level < g_minLevel) return;
    }

    char message[2048];
    va_list args;
    va_start(args, fmt);
    int written = std::vsnprintf(message, sizeof(message), fmt ? fmt : "", args);
    va_end(args);
    if (written < 0) message[0] = '\0';

    char formatted[2200];
    std::snprintf(formatted, sizeof(formatted), "[%s] %s:%d %s",
                  levelName(level), file ? file : "?", line, message);

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_recentLines.emplace_back(formatted);
        if (g_recentLines.size() > kMaxRecentLines) {
            g_recentLines.erase(g_recentLines.begin());
        }
    }

#ifdef __ANDROID__
    int priority = ANDROID_LOG_INFO;
    if (level == LogLevel::Warning) priority = ANDROID_LOG_WARN;
    if (level == LogLevel::Error)   priority = ANDROID_LOG_ERROR;
    __android_log_print(priority, "canvas-cpp", "%s", formatted);
#else
    std::fprintf(stderr, "%s\n", formatted);
    std::fflush(stderr);
#endif

    if (level == LogLevel::Error) incrementErrorCount();
}

} // namespace canvas
