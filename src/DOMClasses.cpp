#include "DOMClasses.h"
#include "Log.h"
#include "RenderNode.h"

namespace canvas {

IMPL_QUICKJS_CLASS(NodeClass)
IMPL_QUICKJS_CLASS(ElementClass)
IMPL_QUICKJS_CLASS(HtmlClass)
IMPL_QUICKJS_CLASS(CanvasClass)
IMPL_QUICKJS_CLASS(DocumentClass)
IMPL_QUICKJS_CLASS(WindowClass)
IMPL_QUICKJS_CLASS(ImageClass)

NodeClass::NodeClass(std::string className) : m_className(std::move(className))
{
}

NodeClass::~NodeClass() = default;

ElementClass::ElementClass(const std::string& tagName)
    : NodeClass("Element"), m_tagName(tagName)
{
}

ElementClass::~ElementClass()
{
    if (!JS_IsUndefined(m_jsValue)) m_jsValue = JS_UNDEFINED;
}

void ElementClass::initJsFields(JSContext* ctx, JSValue object)
{
    if (!JS_IsUndefined(m_jsValue)) JS_FreeValue(ctx, m_jsValue);
    m_jsValue = JS_DupValue(ctx, object);
    JS_SetPropertyStr(ctx, object, "__nativeElement", toJS<ElementClass>(ctx, this));
}

void ElementClass::freeJsFields(JSRuntime*)
{
    m_jsValue = JS_UNDEFINED;
}

StyleClass* ElementClass::getInlineStyle(JSContext* ctx)
{
    if (JS_IsUndefined(m_jsValue)) return &m_inlineStyle;
    JSValue styleValue = JS_GetPropertyStr(ctx, m_jsValue, "style");
    StyleClass* native = fromJS<StyleClass>(styleValue);
    if (native) m_inlineStyle = *native;
    JS_FreeValue(ctx, styleValue);
    return &m_inlineStyle;
}

int ElementClass::getChildCount(JSContext* ctx)
{
    if (JS_IsUndefined(m_jsValue)) return 0;
    JSValue children = JS_GetPropertyStr(ctx, m_jsValue, "_children");
    JSValue length = JS_GetPropertyStr(ctx, children, "length");
    int count = JS_ToInt32(ctx, nullptr, length);
    JS_FreeValue(ctx, length);
    JS_FreeValue(ctx, children);
    return count > 0 ? count : 0;
}

JSValue ElementClass::getChildAt(JSContext* ctx, int index)
{
    if (JS_IsUndefined(m_jsValue)) return JS_UNDEFINED;
    JSValue children = JS_GetPropertyStr(ctx, m_jsValue, "_children");
    JSValue child = JS_GetPropertyUint32(ctx, children, static_cast<uint32_t>(index));
    JS_FreeValue(ctx, children);
    return child;
}

void ElementClass::getBoundingRect(float& x, float& y, float& w, float& h) const
{
    x = m_rect[0]; y = m_rect[1]; w = m_rect[2]; h = m_rect[3];
}

void ElementClass::setBoundingRect(float x, float y, float w, float h)
{
    m_rect[0] = x; m_rect[1] = y; m_rect[2] = w; m_rect[3] = h;
}

ElementClass* elementFromJS(JSContext* ctx, JSValueConst value)
{
    if (ElementClass* direct = fromJS<ElementClass>(value)) return direct;
    JSValue native = JS_GetPropertyStr(ctx, value, "__nativeElement");
    ElementClass* element = fromJS<ElementClass>(native);
    JS_FreeValue(ctx, native);
    return element;
}

HtmlClass::HtmlClass() : ElementClass("html")
{
}

void HtmlClass::setDirtyListenr(RenderNode* listener)
{
    m_dirtyListener = listener;
}

void HtmlClass::markDirty()
{
    if (m_dirtyListener) m_dirtyListener->bubbleDirty(canvas::CONTENT);
}

CanvasClass::CanvasClass() : ElementClass("canvas")
{
}

CanvasRenderingContext2d* CanvasClass::context2d(JSContext*)
{
    if (!m_context) m_context = new CanvasRenderingContext2d(width, height);
    return m_context;
}

void CanvasClass::resize(int w, int h)
{
    width = w;
    height = h;
    if (m_context) m_context->setSize(w, h);
}

DocumentClass::DocumentClass()
    : NodeClass("Document"), m_html(new HtmlClass())
{
}

DocumentClass::~DocumentClass()
{
    delete m_html;
}

void DocumentClass::setHtml(HtmlClass* html)
{
    if (m_html == html) return;
    delete m_html;
    m_html = html;
}

void DocumentClass::freeJsFields(JSRuntime*)
{
}

ImageClass::ImageClass() : NodeClass("Image")
{
}

namespace {

JSValue js_dispatchEvent(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 2) return JS_UNDEFINED;
    const char* type = JS_ToCString(ctx, argv[1]);
    if (!type) return JS_EXCEPTION;

    JSValue properties = argc > 2 ? argv[2] : JS_UNDEFINED;
    JSValue events = JS_GetPropertyStr(ctx, argv[0], "_events");
    JSValue listeners = JS_GetPropertyStr(ctx, events, type);
    if (JS_IsFunction(ctx, listeners)) {
        JSValue result = JS_Call(ctx, listeners, argv[0], 1, &properties);
        JS_FreeValue(ctx, result);
    }
    JS_FreeValue(ctx, listeners);
    JS_FreeValue(ctx, events);

    const std::string handlerName = std::string("on") + type;
    JSValue handler = JS_GetPropertyStr(ctx, argv[0], handlerName.c_str());
    if (JS_IsFunction(ctx, handler)) {
        JSValue result = JS_Call(ctx, handler, argv[0], 1, &properties);
        JS_FreeValue(ctx, result);
    }
    JS_FreeValue(ctx, handler);
    JS_FreeCString(ctx, type);
    return JS_UNDEFINED;
}

JSValue js_createStyle(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    return toJS<StyleClass>(ctx, new StyleClass());
}

JSValue js_setCanvasElementRef(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1) return JS_UNDEFINED;
    ElementClass* element = elementFromJS(ctx, argv[0]);
    if (!element) {
        element = new CanvasClass();
        element->initJsFields(ctx, argv[0]);
    }
    return JS_UNDEFINED;
}

JSValue js_resizeCanvas(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 3) return JS_UNDEFINED;
    ElementClass* element = elementFromJS(ctx, argv[0]);
    auto* canvas = dynamic_cast<CanvasClass*>(element);
    if (!canvas) return JS_UNDEFINED;
    int width = 0, height = 0;
    JS_ToInt32(ctx, &width, argv[1]);
    JS_ToInt32(ctx, &height, argv[2]);
    canvas->resize(width, height);
    return JS_UNDEFINED;
}

JSValue js_canvasToDataURL(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    return JS_NewString(ctx, "");
}

JSValue js_fetch(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "ok", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, result, "status", JS_NewInt32(ctx, 404));
    JS_SetPropertyStr(ctx, result, "statusText", JS_NewString(ctx, "native fetch unavailable"));
    return result;
}

} // namespace

WindowClass::WindowClass()
    : NodeClass("Window"), m_document(new DocumentClass())
{
}

WindowClass::~WindowClass()
{
    delete m_document;
}

void WindowClass::initJsFields(JSContext* ctx, JSValue global)
{
    NodeClass::initJsFields(ctx, global);
    JS_SetPropertyStr(ctx, global, "dispatchEvent",
                      JS_NewCFunction(ctx, js_dispatchEvent, "dispatchEvent", 3));
    JS_SetPropertyStr(ctx, global, "__createStyle",
                      JS_NewCFunction(ctx, js_createStyle, "__createStyle", 0));
    JS_SetPropertyStr(ctx, global, "__setCanvasElementRef",
                      JS_NewCFunction(ctx, js_setCanvasElementRef, "__setCanvasElementRef", 1));
    JS_SetPropertyStr(ctx, global, "__resizeCanvas",
                      JS_NewCFunction(ctx, js_resizeCanvas, "__resizeCanvas", 3));
    JS_SetPropertyStr(ctx, global, "__canvasToDataURL",
                      JS_NewCFunction(ctx, js_canvasToDataURL, "__canvasToDataURL", 1));
    JS_SetPropertyStr(ctx, global, "__fetch",
                      JS_NewCFunction(ctx, js_fetch, "__fetch", 1));

    JSValue document = JS_GetPropertyStr(ctx, global, "document");
    JSValue documentElement = JS_GetPropertyStr(ctx, document, "documentElement");
    if (!JS_IsUndefined(documentElement) && !JS_IsNull(documentElement)) {
        auto* html = new HtmlClass();
        html->initJsFields(ctx, documentElement);
        m_document->setHtml(html);
    }
    JS_FreeValue(ctx, documentElement);
    JS_FreeValue(ctx, document);
}

void WindowClass::freeJsFields(JSRuntime*)
{
}

} // namespace canvas
