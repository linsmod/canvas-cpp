#include "SimpleElement.h"
#include "CanvasElement.h"
#include <quickjs/quickjs.h>
#include <stdio.h>

int main() {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);
    
    // 注册基类
    if (js_init_module_simple_element(ctx, NULL) < 0) {
        fprintf(stderr, "Failed to init SimpleElement\n");
        return 1;
    }
    
    // 注册派生类（必须在基类之后）
    if (js_init_module_canvas_element(ctx, NULL) < 0) {
        fprintf(stderr, "Failed to init CanvasElement\n");
        return 1;
    }
    
    // 测试 JS 代码
    const char* script = R"(
        var canvas = new CanvasElement();
        console.log(canvas.width);          // 300
        console.log(canvas.height);         // 200
        console.log(canvas.id);             // "canvas"
        
        var rect = canvas.getBoundingClientRect();
        console.log(rect.width);             // 100
        
        var ctx = canvas.getContext('2d');
        ctx.fillRect(0, 0, 50, 50);
        
        // 检查继承关系
        console.log(canvas instanceof CanvasElement);  // true
        console.log(canvas instanceof SimpleElement);  // true
    )";
    
    JSValue result = JS_Eval(ctx, script, strlen(script), "<test>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        const char* str = JS_ToCString(ctx, exception);
        fprintf(stderr, "Exception: %s\n", str);
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, exception);
    }
    JS_FreeValue(ctx, result);
    
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}