#pragma once

#include <type_traits>
#include <utility>

#include <quickjs.h>

namespace canvas {

class AnyClass {
public:
    virtual ~AnyClass() = default;

    virtual void initJsFields(JSContext*, JSValue) {}
    virtual void freeJsFields(JSRuntime*) {}
};

namespace detail {

template <typename T, typename = void>
struct HasFreeJsFields : std::false_type {};

template <typename T>
struct HasFreeJsFields<T, decltype(void(std::declval<T&>().freeJsFields(
                              std::declval<JSRuntime*>())))> : std::true_type {};

template <typename T>
void defaultFinalizer(JSRuntime* runtime, JSValue value)
{
    T* object = static_cast<T*>(JS_GetOpaque(value, T::class_id));
    if (!object) return;
    if constexpr (HasFreeJsFields<T>::value) {
        object->freeJsFields(runtime);
    }
    delete object;
}

} // namespace detail

template <typename T>
T* fromJS(JSValueConst value)
{
    return static_cast<T*>(JS_GetOpaque(value, T::class_id));
}

template <typename T>
T* fromJS(JSContext* ctx, JSValueConst value)
{
    return static_cast<T*>(JS_GetOpaque2(ctx, value, T::class_id));
}

template <typename T>
JSValue toJS(JSContext* ctx, T* object)
{
    if (!object) return JS_NULL;
    JSValue value = JS_NewObjectClass(ctx, T::class_id);
    if (JS_IsException(value)) return value;
    JS_SetOpaque(value, object);
    return value;
}

template <typename T>
T* runtimeOpaque(JSRuntime* runtime)
{
    return static_cast<T*>(JS_GetRuntimeOpaque(runtime));
}

template <typename T>
T* contextOpaque(JSContext* ctx)
{
    return static_cast<T*>(JS_GetContextOpaque(ctx));
}

struct Protos {
    JSValue Element;
    JSValue Html;
    JSValue Canvas;
    JSValue Image;
    JSValue Style;
    JSValue Context2d;
    JSValue Event;
    JSValue Orientation;
    JSValue ImageData;

    Protos()
        : Element(JS_UNDEFINED)
        , Html(JS_UNDEFINED)
        , Canvas(JS_UNDEFINED)
        , Image(JS_UNDEFINED)
        , Style(JS_UNDEFINED)
        , Context2d(JS_UNDEFINED)
        , Event(JS_UNDEFINED)
        , Orientation(JS_UNDEFINED)
        , ImageData(JS_UNDEFINED)
    {
    }

    void clear(JSContext* ctx)
    {
        JS_FreeValue(ctx, Element);
        JS_FreeValue(ctx, Html);
        JS_FreeValue(ctx, Canvas);
        JS_FreeValue(ctx, Image);
        JS_FreeValue(ctx, Style);
        JS_FreeValue(ctx, Context2d);
        JS_FreeValue(ctx, Event);
        JS_FreeValue(ctx, Orientation);
        JS_FreeValue(ctx, ImageData);
    }
};

void registerBindings(JSContext* ctx, Protos& protos);

} // namespace canvas

using canvas::AnyClass;
using canvas::contextOpaque;
using canvas::fromJS;
using canvas::Protos;
using canvas::registerBindings;
using canvas::runtimeOpaque;
using canvas::toJS;

#define DECLARE_QUICKJS_CLASS(Type)                                      \
    public:                                                              \
    static JSClassID class_id;                                           \
    static JSClassID getClassID() { return class_id; }                   \
    static void registerClass(JSContext* ctx);

#define IMPL_QUICKJS_CLASS(Type)                                                      \
    JSClassID Type::class_id = 0;                                                     \
    void Type::registerClass(JSContext* ctx)                                          \
    {                                                                                 \
        JS_NewClassID(&Type::class_id);                                               \
        JSClassDef definition = {};                                                   \
        definition.class_name = #Type;                                                \
        definition.finalizer = &::canvas::detail::defaultFinalizer<Type>;             \
        JS_NewClass(JS_GetRuntime(ctx), Type::class_id, &definition);                  \
    }
