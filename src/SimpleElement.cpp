#include "CanvasElement.h"
#include "SimpleElement.h"
#include <quickjs/quickjs.h>
#include <stdio.h>
#include <stdlib.h>

// 子类自己的 Class ID
static JSClassID js_canvas_element_class_id;

// ============================================================
// 内部数据结构（扩展父类）
// ============================================================
typedef struct CanvasElementData {
    int width;
    int height;
    int canvas_id;
} CanvasElementData;

static void canvas_element_finalizer(JSRuntime* rt, JSValue val) {
    CanvasElementData* data = (CanvasElementData*)JS_GetOpaque(val, js_canvas_element_class_id);
    if (data) {
        js_free_rt(rt, data);
    }
}

// ============================================================
// 方法：getContext
// ============================================================
static JSValue js_canvas_element_getContext(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const char* type = "";
    if (argc > 0) {
        type = JS_ToCString(ctx, argv[0]);
        if (!type) return JS_EXCEPTION;
    }
    
    // 模拟返回一个假的 context 对象
    JSValue context = JS_NewObject(ctx);
    if (type && strcmp(type, "2d") == 0) {
        JS_SetPropertyStr(ctx, context, "fillRect", JS_NewCFunction(ctx, 
            [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
                return JS_UNDEFINED;
            }, "fillRect", 4));
    }
    
    if (type) JS_FreeCString(ctx, type);
    return context;
}

// ============================================================
// 构造函数
// ============================================================
static JSValue js_canvas_element_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    // 1. 创建子类实例对象（使用子类的 Class ID）
    JSValue obj = JS_NewObjectClass(ctx, js_canvas_element_class_id);
    if (JS_IsException(obj))
        return obj;
    
    // 2. 初始化子类自己的数据
    CanvasElementData* data = (CanvasElementData*)js_mallocz(ctx, sizeof(CanvasElementData));
    if (!data) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowOutOfMemory(ctx);
    }
    data->width = 300;
    data->height = 200;
    data->canvas_id = 0;
    JS_SetOpaque(obj, data);
    
    // 3. 调用父类构造函数（模拟 `super()`）
    //    获取父类构造函数
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue parent_ctor = JS_GetPropertyStr(ctx, global, "SimpleElement");
    JS_FreeValue(ctx, global);
    
    if (JS_IsFunction(ctx, parent_ctor)) {
        // 使用父类构造函数，传入当前 this (obj) 作为 new.target 的 this
        JSValue result = JS_CallConstructor2(ctx, parent_ctor, new_target, 0, NULL);
        if (JS_IsException(result)) {
            JS_FreeValue(ctx, obj);
            JS_FreeValue(ctx, parent_ctor);
            return result;
        }
        // 注意：JS_CallConstructor2 返回新对象，但我们已经有 obj 了，需要合并属性？
        // 更简单的方式是在子类构造函数中直接复制父类需要的属性，或者让父类构造函数直接操作 this。
        // 由于父类构造函数使用 `JS_NewObjectClass` 会新建对象，不能直接操作传入的 obj。
        // 为了正确继承，应该在父类中提供一个 `init` 方法，或者不使用双构造。
        // 这里为了演示，我们手动设置一些父类所需的属性。
        JS_SetPropertyStr(ctx, obj, "id", JS_NewString(ctx, "canvas"));
        JS_FreeValue(ctx, result);
    }
    JS_FreeValue(ctx, parent_ctor);
    
    // 设置一些属性
    JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, data->width));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, data->height));
    
    return obj;
}

// ============================================================
// 注册子类（并建立继承关系）
// ============================================================
int js_init_module_canvas_element(JSContext* ctx, JSModuleDef* m) {
    // 1. 注册子类的 Class
    JSClassDef canvas_class_def = {
        .class_name = "CanvasElement",
        .finalizer = canvas_element_finalizer,
    };
    JS_NewClassID(&js_canvas_element_class_id);
    int ret = JS_NewClass(JS_GetRuntime(ctx), js_canvas_element_class_id, &canvas_class_def);
    if (ret != 0)
        return ret;
    
    // 2. 获取父类的原型对象
    JSValue parent_proto = js_simple_element_get_prototype(ctx);
    if (JS_IsNull(parent_proto)) {
        fprintf(stderr, "Failed to get parent prototype. Did you register SimpleElement first?\n");
        return -1;
    }
    
    // 3. 创建子类的原型对象，其 [[Prototype]] 指向父类的原型
    JSValue proto = JS_NewObjectProto(ctx, parent_proto);
    JS_FreeValue(ctx, parent_proto);
    if (JS_IsException(proto))
        return -1;
    
    // 4. 添加子类自己的方法
    const JSCFunctionListEntry canvas_proto_funcs[] = {
        JS_CFUNC_DEF("getContext", 1, js_canvas_element_getContext),
    };
    JS_SetPropertyFunctionList(ctx, proto, canvas_proto_funcs,
                               sizeof(canvas_proto_funcs) / sizeof(JSCFunctionListEntry));
    
    // 5. 设置子类的构造函数
    JSValue ctor = JS_NewCFunction2(ctx, js_canvas_element_constructor, "CanvasElement", 0,
                                    JS_CFUNC_constructor, 0);
    if (JS_IsException(ctor)) {
        JS_FreeValue(ctx, proto);
        return -1;
    }
    JS_SetConstructor(ctx, ctor, proto);
    JS_SetClassProto(ctx, js_canvas_element_class_id, proto);
    
    // 6. 导出或挂载到全局
    if (m) {
        ret = JS_SetModuleExport(ctx, m, "CanvasElement", ctor);
        if (ret < 0) {
            JS_FreeValue(ctx, ctor);
            return ret;
        }
    } else {
        JSValue global = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, global, "CanvasElement", ctor);
        JS_FreeValue(ctx, global);
    }
    
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, proto);
    return 0;
}