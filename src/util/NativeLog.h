#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace canvas {

enum class LogLevel { Info = 0, Warning = 1, Error = 2 };

void nativeLog(LogLevel level, const char* file, int line, const char* fmt, ...);
void setNativeLogMinLevel(LogLevel level);

std::vector<std::string> recentLogLines(std::size_t maxLines);
void clearRecentLog();

class NativeLog {
public:
    static std::string pop();
    static std::vector<std::string> recent(std::size_t maxLines = 1);
};

} // namespace canvas
