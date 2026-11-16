#include "Orientation.h"
#include "util/OrientationBridge.h"

namespace canvas {

IMPL_QUICKJS_CLASS(Orientation)

Orientation::Orientation()
{
    refresh();
}

void Orientation::refresh()
{
    const char* value = currentOrientationType();
    type = value ? value : "portrait-primary";
    angle = currentOrientationAngle();
}

JSValue js_orientation_type_get(JSContext* ctx, JSValueConst this_val)
{
    Orientation* orientation = fromJS<Orientation>(ctx, this_val);
    if (!orientation) return JS_EXCEPTION;
    orientation->refresh();
    return JS_NewString(ctx, orientation->type.c_str());
}

JSValue js_orientation_angle_get(JSContext* ctx, JSValueConst this_val)
{
    Orientation* orientation = fromJS<Orientation>(ctx, this_val);
    if (!orientation) return JS_EXCEPTION;
    orientation->refresh();
    return JS_NewInt32(ctx, orientation->angle);
}

} // namespace canvas
