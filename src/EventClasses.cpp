#include "EventClasses.h"

namespace canvas {
namespace {

void setString(JSContext* ctx, JSValue object, const char* name, const std::string& value)
{
    JS_SetPropertyStr(ctx, object, name, JS_NewString(ctx, value.c_str()));
}

void setInt(JSContext* ctx, JSValue object, const char* name, int value)
{
    JS_SetPropertyStr(ctx, object, name, JS_NewInt32(ctx, value));
}

void setBool(JSContext* ctx, JSValue object, const char* name, bool value)
{
    JS_SetPropertyStr(ctx, object, name, JS_NewBool(ctx, value ? 1 : 0));
}

} // namespace

JSValue Event::toJS(JSContext* ctx) const
{
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) return object;
    setString(ctx, object, "type", type);
    setBool(ctx, object, "bubbles", bubbles);
    setBool(ctx, object, "cancelable", cancelable);
    return object;
}

JSValue UIEvent::toJS(JSContext* ctx) const
{
    JSValue object = Event::toJS(ctx);
    if (JS_IsException(object)) return object;
    setInt(ctx, object, "detail", detail);
    return object;
}

JSValue MouseEvent::toJS(JSContext* ctx) const
{
    JSValue object = UIEvent::toJS(ctx);
    if (JS_IsException(object)) return object;
    setInt(ctx, object, "clientX", clientX);
    setInt(ctx, object, "clientY", clientY);
    setInt(ctx, object, "button", button);
    setInt(ctx, object, "buttons", buttons);
    setBool(ctx, object, "ctrlKey", ctrlKey);
    setBool(ctx, object, "shiftKey", shiftKey);
    setBool(ctx, object, "altKey", altKey);
    setBool(ctx, object, "metaKey", metaKey);
    return object;
}

JSValue WheelEvent::toJS(JSContext* ctx) const
{
    JSValue object = MouseEvent::toJS(ctx);
    if (JS_IsException(object)) return object;
    setInt(ctx, object, "deltaY", deltaY);
    return object;
}

JSValue KeyboardEvent::toJS(JSContext* ctx) const
{
    JSValue object = UIEvent::toJS(ctx);
    if (JS_IsException(object)) return object;
    setString(ctx, object, "key", key);
    setBool(ctx, object, "ctrlKey", ctrlKey);
    setBool(ctx, object, "shiftKey", shiftKey);
    setBool(ctx, object, "altKey", altKey);
    setBool(ctx, object, "metaKey", metaKey);
    return object;
}

JSValue FocusEvent::toJS(JSContext* ctx) const
{
    JSValue object = UIEvent::toJS(ctx);
    if (JS_IsException(object)) return object;
    setBool(ctx, object, "focused", focused);
    return object;
}

JSValue eventToJS(JSContext* ctx, const Event& event)
{
    return event.toJS(ctx);
}

} // namespace canvas
