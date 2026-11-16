#include "Bindings.h"
#include "DOMClasses.h"
#include "Orientation.h"

namespace canvas {

void registerBindings(JSContext* ctx, Protos& protos)
{
    StyleClass::registerClass(ctx);
    NodeClass::registerClass(ctx);
    ElementClass::registerClass(ctx);
    HtmlClass::registerClass(ctx);
    CanvasClass::registerClass(ctx);
    DocumentClass::registerClass(ctx);
    WindowClass::registerClass(ctx);
    ImageClass::registerClass(ctx);
    Orientation::registerClass(ctx);

    protos.Element = JS_NewObject(ctx);
    protos.Html = JS_NewObjectProto(ctx, protos.Element);
    protos.Canvas = JS_NewObjectProto(ctx, protos.Element);
    protos.Image = JS_NewObject(ctx);
    protos.Style = JS_NewObject(ctx);
    protos.Context2d = JS_NewObject(ctx);
    protos.Event = JS_NewObject(ctx);
    protos.Orientation = toJS<Orientation>(ctx, new Orientation());
    protos.ImageData = JS_NewObject(ctx);
}

} // namespace canvas
