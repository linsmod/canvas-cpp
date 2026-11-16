#include "Bindings.h"
#include "CanvasAppBase.h"
#include "DOMClasses.h"
#include "RenderNode.h"

namespace canvas {

JSValue js_element_getBoundingClientRect(JSContext* ctx, JSValueConst this_val,
                                         int, JSValueConst*)
{
    ElementClass* element = elementFromJS(ctx, this_val);
    if (!element) return JS_ThrowTypeError(ctx, "Not an Element");

    auto* app = runtimeOpaque<CanvasAppBase>(JS_GetRuntime(ctx));
    RenderNode* node = app ? findRenderNode(app->renderTree(), element) : nullptr;

    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    if (node) node->getBoundingRect(x, y, w, h);

    JSValue rect = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, rect, "x", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, rect, "y", JS_NewFloat64(ctx, y));
    JS_SetPropertyStr(ctx, rect, "width", JS_NewFloat64(ctx, w));
    JS_SetPropertyStr(ctx, rect, "height", JS_NewFloat64(ctx, h));
    JS_SetPropertyStr(ctx, rect, "top", JS_NewFloat64(ctx, y));
    JS_SetPropertyStr(ctx, rect, "left", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, rect, "right", JS_NewFloat64(ctx, x + w));
    JS_SetPropertyStr(ctx, rect, "bottom", JS_NewFloat64(ctx, y + h));
    return rect;
}

} // namespace canvas
