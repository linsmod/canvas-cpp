#pragma once

#include <quickjs.h>

namespace canvas {

void printException(JSContext* ctx, JSValue exception, const char* prefix = nullptr);

} // namespace canvas
