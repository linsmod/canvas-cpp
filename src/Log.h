#pragma once

#include <string>
#include "util/NativeLog.h"

#define LOGI(...) ::canvas::nativeLog(::canvas::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...) ::canvas::nativeLog(::canvas::LogLevel::Warning, __FILE__, __LINE__, __VA_ARGS__)
#define LOGE(...) ::canvas::nativeLog(::canvas::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
