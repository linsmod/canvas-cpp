#include "EventDispatch.h"
#include "EventClasses.h"
#include "util/JSError.h"

namespace canvas {
namespace {

void callDispatch(JSContext* ctx, JSValue target, const char* type, JSValue properties)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue handler = JS_GetPropertyStr(ctx, global, "dispatchEvent");
    if (JS_IsFunction(ctx, handler)) {
        JSValue typeValue = JS_NewString(ctx, type);
        JSValue args[3] = {target, typeValue, properties};
        JSValue result = JS_Call(ctx, handler, global, 3, args);
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(ctx);
            printException(ctx, exception, "dispatchEvent: ");
            JS_FreeValue(ctx, exception);
        }
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, typeValue);
    }
    JS_FreeValue(ctx, handler);
    JS_FreeValue(ctx, global);
}

void dispatchToWindow(JSContext* ctx, const char* type, const Event& event)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue properties = eventToJS(ctx, event);
    callDispatch(ctx, global, type, properties);
    JS_FreeValue(ctx, properties);
    JS_FreeValue(ctx, global);
}

JSValue resolvePointerTarget(JSContext* ctx, int x, int y)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue doc = JS_GetPropertyStr(ctx, global, "document");
    JSValue lookup = JS_GetPropertyStr(ctx, doc, "elementFromPoint");
    if (JS_IsFunction(ctx, lookup)) {
        JSValue xValue = JS_NewInt32(ctx, x);
        JSValue yValue = JS_NewInt32(ctx, y);
        JSValue args[2] = {xValue, yValue};
        JSValue element = JS_Call(ctx, lookup, doc, 2, args);
        JS_FreeValue(ctx, xValue);
        JS_FreeValue(ctx, yValue);
        JS_FreeValue(ctx, lookup);
        JS_FreeValue(ctx, global);
        if (!JS_IsException(element) && !JS_IsNull(element) && !JS_IsUndefined(element)) {
            return element;
        }
        JS_FreeValue(ctx, element);
        return doc;
    }
    JS_FreeValue(ctx, lookup);
    JS_FreeValue(ctx, global);
    return doc;
}

} // namespace

void dispatchMouseEvent(JSContext* ctx, const char* type, int x, int y,
                        int button, int buttons)
{
    MouseEvent event(type ? type : "mousemove");
    event.clientX = x;
    event.clientY = y;
    event.button = button;
    event.buttons = buttons;

    JSValue target = resolvePointerTarget(ctx, x, y);
    JSValue properties = eventToJS(ctx, event);
    callDispatch(ctx, target, event.type.c_str(), properties);
    JS_FreeValue(ctx, properties);
    JS_FreeValue(ctx, target);
}

void dispatchWheelEvent(JSContext* ctx, const char* type, int x, int y,
                        int deltaY, bool ctrl, bool shift, bool alt, bool meta)
{
    WheelEvent event(type ? type : "wheel");
    event.clientX = x;
    event.clientY = y;
    event.deltaY = deltaY;
    event.ctrlKey = ctrl;
    event.shiftKey = shift;
    event.altKey = alt;
    event.metaKey = meta;

    JSValue target = resolvePointerTarget(ctx, x, y);
    JSValue properties = eventToJS(ctx, event);
    callDispatch(ctx, target, event.type.c_str(), properties);
    JS_FreeValue(ctx, properties);
    JS_FreeValue(ctx, target);
}

void dispatchKeyEvent(JSContext* ctx, const char* type, const char* key,
                      bool ctrl, bool shift, bool alt, bool meta)
{
    KeyboardEvent event(type ? type : "keydown");
    event.key = key ? key : "";
    event.ctrlKey = ctrl;
    event.shiftKey = shift;
    event.altKey = alt;
    event.metaKey = meta;
    dispatchToWindow(ctx, event.type.c_str(), event);
}

void dispatchWindowFocusEvent(JSContext* ctx, bool focused)
{
    FocusEvent event(focused ? "focus" : "blur");
    event.focused = focused;
    dispatchToWindow(ctx, event.type.c_str(), event);
}

void dispatchVisibilityChanged(JSContext* ctx, bool hidden)
{
    Event event(hidden ? "visibilitychange" : "visibilitychange");
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue properties = eventToJS(ctx, event);
    JS_SetPropertyStr(ctx, properties, "hidden", JS_NewBool(ctx, hidden ? 1 : 0));
    JS_SetPropertyStr(ctx, properties, "visibilityState",
                      JS_NewString(ctx, hidden ? "hidden" : "visible"));
    callDispatch(ctx, global, event.type.c_str(), properties);
    JS_FreeValue(ctx, properties);
    JS_FreeValue(ctx, global);
}

void dispatchOrientationChanged(JSContext* ctx)
{
    Event event("orientationchange");
    dispatchToWindow(ctx, event.type.c_str(), event);
}

void dispatchDOMContentLoaded(JSContext* ctx)
{
    Event event("DOMContentLoaded");
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue document = JS_GetPropertyStr(ctx, global, "document");
    JSValue properties = eventToJS(ctx, event);
    callDispatch(ctx, document, event.type.c_str(), properties);
    JS_FreeValue(ctx, properties);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, global);
}

void dispatchWindowLoadEvent(JSContext* ctx)
{
    Event event("load");
    dispatchToWindow(ctx, event.type.c_str(), event);
}

} // namespace canvas
