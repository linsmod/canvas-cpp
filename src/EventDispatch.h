#pragma once

#include <quickjs.h>

namespace canvas {

void dispatchMouseEvent(JSContext* ctx, const char* type, int x, int y,
                        int button, int buttons);
void dispatchWheelEvent(JSContext* ctx, const char* type, int x, int y,
                        int deltaY, bool ctrl, bool shift, bool alt, bool meta);
void dispatchKeyEvent(JSContext* ctx, const char* type, const char* key,
                      bool ctrl, bool shift, bool alt, bool meta);
void dispatchWindowFocusEvent(JSContext* ctx, bool focused);
void dispatchVisibilityChanged(JSContext* ctx, bool hidden);
void dispatchOrientationChanged(JSContext* ctx);
void dispatchDOMContentLoaded(JSContext* ctx);
void dispatchWindowLoadEvent(JSContext* ctx);

} // namespace canvas
