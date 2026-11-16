#include "JSError.h"
#include "Log.h"

namespace canvas {

void printException(JSContext* ctx, JSValue exception, const char* prefix)
{
    const char* message = JS_ToCString(ctx, exception);
    LOGE("%s%s", prefix ? prefix : "", message ? message : "(unknown exception)");
    if (message) JS_FreeCString(ctx, message);

    if (!JS_IsObject(exception)) return;

    JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
    if (!JS_IsUndefined(stack) && !JS_IsNull(stack)) {
        const char* text = JS_ToCString(ctx, stack);
        if (text) {
            LOGE("%s", text);
            JS_FreeCString(ctx, text);
        }
    }
    JS_FreeValue(ctx, stack);
}

} // namespace canvas
