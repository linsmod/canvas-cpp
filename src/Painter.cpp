/**
 * Copyright (c) 2010 Daniel Wiberg
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "Painter.h"
#include "quickjs.h"
#include "util/ScopedLock.h"
#include "quickjs-libc.h"
#include "browser_shim.h"
#include "ScriptLoader.h"

#include <iostream>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <string>
#include <vector>

namespace canvas
{
   // Static class IDs for QuickJS wrapped objects
   static JSClassID s_ctx_class_id = 0;
   static JSClassID s_imagedata_class_id = 0;


   
   // Helper to convert JSValue to float
   static inline float jsval_to_float(JSContext *ctx, JSValueConst v)
   {
      double d;
      JS_ToFloat64(ctx, &d, v);
      return static_cast<float>(d);
   }
   
   static inline void jsval_to_float_ptr(JSContext *ctx, float *out, JSValueConst v)
   {
      double d;
      JS_ToFloat64(ctx, &d, v);
      *out = static_cast<float>(d);
   }
  
   Painter::Painter(int width, int height, Canvas::Format format)
      : m_painterMutex(),
        m_imageMutex(),
        m_logMutex(),
        m_width(width),
        m_height(height),
        m_format(format),
        m_script(0),
        m_context(0),
         m_callbackIndex(0),
          m_runtime(0),
          m_jsContext(0),
          m_rafIndex(0)
   {
      m_canvasElement = JS_UNDEFINED;
   }
   
   void Painter::start(std::string const& fileOrCode, bool isFile)
   {
      ScopedLock lock(m_painterMutex);
      
      // Create QuickJS runtime and context
      m_runtime = JS_NewRuntime();
      
      // ES module loader (resolves import/export across files)
      JS_SetModuleLoaderFunc2(m_runtime, NULL, js_module_loader, js_module_check_attributes, NULL);
      
      m_jsContext = JS_NewContext(m_runtime);
      
      // Store this Painter* as context opaque data for callbacks to use
      JS_SetContextOpaque(m_jsContext, this);
      
      // Register class IDs (only once per runtime)
      if (s_ctx_class_id == 0)
         JS_NewClassID(&s_ctx_class_id);
      if (s_imagedata_class_id == 0)
         JS_NewClassID(&s_imagedata_class_id);
      
      JSClassDef ctx_class_def = {};
      ctx_class_def.class_name = "Context2D";
      JS_NewClass(m_runtime, s_ctx_class_id, &ctx_class_def);
      
      JSClassDef img_class_def = {};
      img_class_def.class_name = "ImageData";
      JS_NewClass(m_runtime, s_imagedata_class_id, &img_class_def);
      
      // Register all JS bindings
      registerBindings();
      
      // Create graphics context
      m_context = new Context(m_width, m_height, m_format);
      
       // Create javascript object and run script
       m_script = new Script(m_jsContext);

       // Evaluate browser shim (provides document, window, etc.)
       {
          std::string shimSource(BROWSER_SHIM_JS, BROWSER_SHIM_JS_LEN);
          if (!m_script->runString(shimSource, "<browser-shim>"))
          {
             std::cerr << "Failed to evaluate browser shim" << std::endl;
             return;
          }
       }

       if (isFile)
          m_script->load(fileOrCode);
       else
          m_script->runString(fileOrCode);
         
      m_timer.reset();
      m_rafTimer.reset();
   }
   
   void Painter::startWithModule(std::string const& entryFile, std::string const& entryDir)
   {
      ScopedLock lock(m_painterMutex);

      m_entryDir = entryDir;

      // Create QuickJS runtime and context
      m_runtime = JS_NewRuntime();

      // ES module loader (resolves import/export across filesystem)
      JS_SetModuleLoaderFunc2(m_runtime, NULL, js_module_loader, js_module_check_attributes, NULL);

      m_jsContext = JS_NewContext(m_runtime);

      // Store this Painter* as context opaque data for callbacks to use
      JS_SetContextOpaque(m_jsContext, this);

      // Register class IDs (only once per runtime)
      if (s_ctx_class_id == 0)
         JS_NewClassID(&s_ctx_class_id);
      if (s_imagedata_class_id == 0)
         JS_NewClassID(&s_imagedata_class_id);

      JSClassDef ctx_class_def = {};
      ctx_class_def.class_name = "Context2D";
      JS_NewClass(m_runtime, s_ctx_class_id, &ctx_class_def);

      JSClassDef img_class_def = {};
      img_class_def.class_name = "ImageData";
      JS_NewClass(m_runtime, s_imagedata_class_id, &img_class_def);

      // Register all JS bindings
      registerBindings();

      // Create graphics context
      m_context = new Context(m_width, m_height, m_format);

      // Create script runner
      m_script = new Script(m_jsContext);

      // Step 1: Evaluate browser shim as global script (provides document, window, etc.)
      std::string shimSource(BROWSER_SHIM_JS, BROWSER_SHIM_JS_LEN);
      if (!m_script->runString(shimSource, "<browser-shim>"))
      {
         std::cerr << "Failed to evaluate browser shim" << std::endl;
         return;
      }

      // Step 2: Read and evaluate entry file as ES module
      // The module loader handles all import resolution from here
      std::string entrySource = ScriptLoader::readFile(entryFile);
      if (entrySource.empty())
      {
         std::cerr << "Failed to read entry module: " << entryFile << std::endl;
         return;
      }

      if (!m_script->runModule(entrySource, entryFile))
      {
         std::cerr << "Failed to load entry module: " << entryFile << std::endl;
         return;
      }

      std::cerr << "[Painter] TOP-LEVEL MODULE EXECUTION COMPLETE" << std::endl;
      m_timer.reset();
      m_rafTimer.reset();
   }
   
   void Painter::registerBindings()
   {
      // global is window and window is global
      JSValue global = JS_GetGlobalObject(m_jsContext);
      
      // Window methods
      JS_SetPropertyStr(m_jsContext, global, "setInterval",
         JS_NewCFunction(m_jsContext, js_setInterval, "setInterval", 2));
       JS_SetPropertyStr(m_jsContext, global, "clearInterval",
          JS_NewCFunction(m_jsContext, js_clearInterval, "clearInterval", 1));
       JS_SetPropertyStr(m_jsContext, global, "setTimeout",
          JS_NewCFunction(m_jsContext, js_setTimeout, "setTimeout", 2));
       JS_SetPropertyStr(m_jsContext, global, "clearTimeout",
          JS_NewCFunction(m_jsContext, js_clearTimeout, "clearTimeout", 1));
       JS_SetPropertyStr(m_jsContext, global, "getContext",
         JS_NewCFunction(m_jsContext, js_getContext, "getContext", 1));
      JS_SetPropertyStr(m_jsContext, global, "getImage",
         JS_NewCFunction(m_jsContext, js_getImage, "getImage", 1));
      JS_SetPropertyStr(m_jsContext, global, "__setCanvasElementRef",
         JS_NewCFunction(m_jsContext, js_setCanvasElementRef, "__setCanvasElementRef", 1));
      JS_SetPropertyStr(m_jsContext, global, "__readFile",
         JS_NewCFunction(m_jsContext, js_readFile, "__readFile", 2));
       JS_SetPropertyStr(m_jsContext, global, "__fetch",
          JS_NewCFunction(m_jsContext, js_fetch, "__fetch", 1));
       JS_SetPropertyStr(m_jsContext, global, "__loadImage",
          JS_NewCFunction(m_jsContext, js_loadImage, "__loadImage", 1));
       JS_SetPropertyStr(m_jsContext, global, "__canvasToDataURL",
          JS_NewCFunction(m_jsContext, js_canvasToDataURL, "__canvasToDataURL", 1));
       
        // Event listener native implementations
        {
           JSValue fn1 = JS_NewCFunction(m_jsContext, js_addEventListener, "addEventListener", 2);
           JSValue fn2 = JS_DupValue(m_jsContext, fn1);
           JS_SetPropertyStr(m_jsContext, global, "addEventListener", fn1);
        }
        {
           JSValue fn1 = JS_NewCFunction(m_jsContext, js_removeEventListener, "removeEventListener", 2);
           JSValue fn2 = JS_DupValue(m_jsContext, fn1);
           JS_SetPropertyStr(m_jsContext, global, "removeEventListener", fn1);
        }
        {
           JSValue fn1 = JS_NewCFunction(m_jsContext, js_dispatchEvent, "dispatchEvent", 3);
           JSValue fn2 = JS_DupValue(m_jsContext, fn1);
           JS_SetPropertyStr(m_jsContext, global, "dispatchEvent", fn1);
        }
        {
           JSValue fn1 = JS_NewCFunction(m_jsContext, js_cleanupEventListeners, "cleanupEventListeners", 0);
           JSValue fn2 = JS_DupValue(m_jsContext, fn1);
           JS_SetPropertyStr(m_jsContext, global, "cleanupEventListeners", fn1);
        }
        
        // Event factory native implementations
        {
           JSValue fn1 = JS_NewCFunction(m_jsContext, js_createKeyEvent, "createKeyEvent", 4);
           JSValue fn2 = JS_DupValue(m_jsContext, fn1);
           JS_SetPropertyStr(m_jsContext, global, "createKeyEvent", fn1);
        }
        {
           JSValue fn1 = JS_NewCFunction(m_jsContext, js_createMouseEvent, "createMouseEvent", 3);
           JSValue fn2 = JS_DupValue(m_jsContext, fn1);
           JS_SetPropertyStr(m_jsContext, global, "createMouseEvent", fn1);
        }
        {
           JSValue fn1 = JS_NewCFunction(m_jsContext, js_createWheelEvent, "createWheelEvent", 6);
           JSValue fn2 = JS_DupValue(m_jsContext, fn1);
           JS_SetPropertyStr(m_jsContext, global, "createWheelEvent", fn1);
        }
        {
           JSValue fn1 = JS_NewCFunction(m_jsContext, js_createTouchEvent, "createTouchEvent", 2);
           JSValue fn2 = JS_DupValue(m_jsContext, fn1);
           JS_SetPropertyStr(m_jsContext, global, "createTouchEvent", fn1);
        }
       
        // window.width and window.height as getter-only properties
       {
          JSCFunctionType get_fn;
          get_fn.getter = js_window_width_get;
          JS_DefinePropertyGetSet(m_jsContext, global,
             JS_NewAtom(m_jsContext, "width"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get width", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          get_fn.getter = js_window_height_get;
          JS_DefinePropertyGetSet(m_jsContext, global,
             JS_NewAtom(m_jsContext, "height"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get height", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          // innerWidth/innerHeight/outerWidth/outerHeight
          get_fn.getter = js_window_width_get;
          JS_DefinePropertyGetSet(m_jsContext, global,
             JS_NewAtom(m_jsContext, "innerWidth"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get innerWidth", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          get_fn.getter = js_window_height_get;
          JS_DefinePropertyGetSet(m_jsContext, global,
             JS_NewAtom(m_jsContext, "innerHeight"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get innerHeight", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          get_fn.getter = js_window_width_get;
          JS_DefinePropertyGetSet(m_jsContext, global,
             JS_NewAtom(m_jsContext, "outerWidth"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get outerWidth", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          get_fn.getter = js_window_height_get;
          JS_DefinePropertyGetSet(m_jsContext, global,
             JS_NewAtom(m_jsContext, "outerHeight"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get outerHeight", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          // devicePixelRatio - always 1
          JS_DefinePropertyValue(m_jsContext, global,
             JS_NewAtom(m_jsContext, "devicePixelRatio"),
             JS_NewFloat64(m_jsContext, 1.0),
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
       }
       
       // window.screen sub-object
       {
          JSValue screen = JS_NewObject(m_jsContext);
          JSCFunctionType get_fn;
          
          get_fn.getter = js_window_width_get;
          JS_DefinePropertyGetSet(m_jsContext, screen,
             JS_NewAtom(m_jsContext, "width"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get width", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          get_fn.getter = js_window_height_get;
          JS_DefinePropertyGetSet(m_jsContext, screen,
             JS_NewAtom(m_jsContext, "height"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get height", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          get_fn.getter = js_window_width_get;
          JS_DefinePropertyGetSet(m_jsContext, screen,
             JS_NewAtom(m_jsContext, "availWidth"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get availWidth", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          get_fn.getter = js_window_height_get;
          JS_DefinePropertyGetSet(m_jsContext, screen,
             JS_NewAtom(m_jsContext, "availHeight"),
             JS_NewCFunction2(m_jsContext, get_fn.generic, "get availHeight", 0, JS_CFUNC_getter, 0),
             JS_UNDEFINED,
             JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
          
          JS_SetPropertyStr(m_jsContext, global, "screen", screen);
       }
       // Register window as global
       JS_SetPropertyStr(m_jsContext, global, "window", global);
       
       // Also set screen on global for direct access
       JSValue screenObj = JS_GetPropertyStr(m_jsContext, global, "screen");
       if (!JS_IsUndefined(screenObj))
       {
          JS_SetPropertyStr(m_jsContext, global, "screen", screenObj);
       }
       else
       {
          JS_FreeValue(m_jsContext, screenObj);
       }
      
       JS_SetPropertyStr(m_jsContext, global, "requestAnimationFrame",
          JS_NewCFunction(m_jsContext, js_requestAnimationFrame, "requestAnimationFrame", 1));
       JS_SetPropertyStr(m_jsContext, global, "cancelAnimationFrame",
          JS_NewCFunction(m_jsContext, js_cancelAnimationFrame, "cancelAnimationFrame", 1));
       
       // === Console ===
       JSValue console = JS_NewObject(m_jsContext);
       JS_SetPropertyStr(m_jsContext, console, "log",
          JS_NewCFunction(m_jsContext, js_console_log, "log", 10));
      JS_SetPropertyStr(m_jsContext, global, "console", console);

      JS_SetPropertyStr(m_jsContext, global, "__debug",
         JS_NewCFunction(m_jsContext, js_debug, "__debug", 1));
      
      // === Context2D class prototype ===
      {
         JSValue ctx_proto = JS_NewObject(m_jsContext);
         
         // Methods
         JS_SetPropertyStr(m_jsContext, ctx_proto, "fillRect",
            JS_NewCFunction(m_jsContext, js_ctx_fillRect, "fillRect", 4));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "strokeRect",
            JS_NewCFunction(m_jsContext, js_ctx_strokeRect, "strokeRect", 4));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "clearRect",
            JS_NewCFunction(m_jsContext, js_ctx_clearRect, "clearRect", 4));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "clear",
            JS_NewCFunction(m_jsContext, js_ctx_clear, "clear", 0));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "fill",
            JS_NewCFunction(m_jsContext, js_ctx_fill, "fill", 0));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "stroke",
            JS_NewCFunction(m_jsContext, js_ctx_stroke, "stroke", 0));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "beginPath",
            JS_NewCFunction(m_jsContext, js_ctx_beginPath, "beginPath", 0));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "closePath",
            JS_NewCFunction(m_jsContext, js_ctx_closePath, "closePath", 0));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "moveTo",
            JS_NewCFunction(m_jsContext, js_ctx_moveTo, "moveTo", 2));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "lineTo",
            JS_NewCFunction(m_jsContext, js_ctx_lineTo, "lineTo", 2));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "quadraticCurveTo",
            JS_NewCFunction(m_jsContext, js_ctx_quadraticCurveTo, "quadraticCurveTo", 4));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "bezierCurveTo",
            JS_NewCFunction(m_jsContext, js_ctx_bezierCurveTo, "bezierCurveTo", 6));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "arcTo",
            JS_NewCFunction(m_jsContext, js_ctx_arcTo, "arcTo", 5));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "rect",
            JS_NewCFunction(m_jsContext, js_ctx_rect, "rect", 4));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "scale",
            JS_NewCFunction(m_jsContext, js_ctx_scale, "scale", 2));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "rotate",
            JS_NewCFunction(m_jsContext, js_ctx_rotate, "rotate", 1));
         JS_SetPropertyStr(m_jsContext, ctx_proto, "translate",
            JS_NewCFunction(m_jsContext, js_ctx_translate, "translate", 2));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "drawImage",
           JS_NewCFunction(m_jsContext, js_ctx_drawImage, "drawImage", 5));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "save",
           JS_NewCFunction(m_jsContext, js_ctx_save, "save", 0));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "restore",
           JS_NewCFunction(m_jsContext, js_ctx_restore, "restore", 0));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "setTransform",
           JS_NewCFunction(m_jsContext, js_ctx_setTransform, "setTransform", 6));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "resetTransform",
           JS_NewCFunction(m_jsContext, js_ctx_resetTransform, "resetTransform", 0));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "arc",
           JS_NewCFunction(m_jsContext, js_ctx_arc, "arc", 5));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "ellipse",
           JS_NewCFunction(m_jsContext, js_ctx_ellipse, "ellipse", 7));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "clip",
           JS_NewCFunction(m_jsContext, js_ctx_clip, "clip", 0));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "fillText",
           JS_NewCFunction(m_jsContext, js_ctx_fillText, "fillText", 3));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "strokeText",
           JS_NewCFunction(m_jsContext, js_ctx_strokeText, "strokeText", 3));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "measureText",
           JS_NewCFunction(m_jsContext, js_ctx_measureText, "measureText", 1));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "createLinearGradient",
           JS_NewCFunction(m_jsContext, js_ctx_createLinearGradient, "createLinearGradient", 4));
        JS_SetPropertyStr(m_jsContext, ctx_proto, "createRadialGradient",
           JS_NewCFunction(m_jsContext, js_ctx_createRadialGradient, "createRadialGradient", 6));
        
        // Properties (getter/setter)
         {
            JSCFunctionType get_fn, set_fn;
            
            get_fn.getter = js_ctx_lineWidth_get;
            set_fn.setter = js_ctx_lineWidth_set;
            JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
               JS_NewAtom(m_jsContext, "lineWidth"),
               JS_NewCFunction2(m_jsContext, get_fn.generic, "get lineWidth", 0, JS_CFUNC_getter, 0),
               JS_NewCFunction2(m_jsContext, set_fn.generic, "set lineWidth", 1, JS_CFUNC_setter, 0),
               JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
            
            get_fn.getter = js_ctx_lineCap_get;
            set_fn.setter = js_ctx_lineCap_set;
            JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
               JS_NewAtom(m_jsContext, "lineCap"),
               JS_NewCFunction2(m_jsContext, get_fn.generic, "get lineCap", 0, JS_CFUNC_getter, 0),
               JS_NewCFunction2(m_jsContext, set_fn.generic, "set lineCap", 1, JS_CFUNC_setter, 0),
               JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
            
            get_fn.getter = js_ctx_strokeStyle_get;
            set_fn.setter = js_ctx_strokeStyle_set;
            JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
               JS_NewAtom(m_jsContext, "strokeStyle"),
               JS_NewCFunction2(m_jsContext, get_fn.generic, "get strokeStyle", 0, JS_CFUNC_getter, 0),
               JS_NewCFunction2(m_jsContext, set_fn.generic, "set strokeStyle", 1, JS_CFUNC_setter, 0),
               JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
            
            get_fn.getter = js_ctx_fillStyle_get;
            set_fn.setter = js_ctx_fillStyle_set;
            JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
               JS_NewAtom(m_jsContext, "fillStyle"),
               JS_NewCFunction2(m_jsContext, get_fn.generic, "get fillStyle", 0, JS_CFUNC_getter, 0),
               JS_NewCFunction2(m_jsContext, set_fn.generic, "set fillStyle", 1, JS_CFUNC_setter, 0),
               JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
            
            get_fn.getter = js_ctx_globalAlpha_get;
            set_fn.setter = js_ctx_globalAlpha_set;
            JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
               JS_NewAtom(m_jsContext, "globalAlpha"),
               JS_NewCFunction2(m_jsContext, get_fn.generic, "get globalAlpha", 0, JS_CFUNC_getter, 0),
            JS_NewCFunction2(m_jsContext, set_fn.generic, "set globalAlpha", 1, JS_CFUNC_setter, 0),
               JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
           
           get_fn.getter = js_ctx_font_get;
           set_fn.setter = js_ctx_font_set;
           JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
              JS_NewAtom(m_jsContext, "font"),
              JS_NewCFunction2(m_jsContext, get_fn.generic, "get font", 0, JS_CFUNC_getter, 0),
              JS_NewCFunction2(m_jsContext, set_fn.generic, "set font", 1, JS_CFUNC_setter, 0),
              JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
           
           get_fn.getter = js_ctx_textAlign_get;
           set_fn.setter = js_ctx_textAlign_set;
           JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
              JS_NewAtom(m_jsContext, "textAlign"),
              JS_NewCFunction2(m_jsContext, get_fn.generic, "get textAlign", 0, JS_CFUNC_getter, 0),
              JS_NewCFunction2(m_jsContext, set_fn.generic, "set textAlign", 1, JS_CFUNC_setter, 0),
              JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
           
           get_fn.getter = js_ctx_textBaseline_get;
           set_fn.setter = js_ctx_textBaseline_set;
           JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
              JS_NewAtom(m_jsContext, "textBaseline"),
              JS_NewCFunction2(m_jsContext, get_fn.generic, "get textBaseline", 0, JS_CFUNC_getter, 0),
              JS_NewCFunction2(m_jsContext, set_fn.generic, "set textBaseline", 1, JS_CFUNC_setter, 0),
              JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
           
           get_fn.getter = js_ctx_globalCompositeOperation_get;
           set_fn.setter = js_ctx_globalCompositeOperation_set;
           JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
              JS_NewAtom(m_jsContext, "globalCompositeOperation"),
              JS_NewCFunction2(m_jsContext, get_fn.generic, "get globalCompositeOperation", 0, JS_CFUNC_getter, 0),
              JS_NewCFunction2(m_jsContext, set_fn.generic, "set globalCompositeOperation", 1, JS_CFUNC_setter, 0),
              JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
           
           get_fn.getter = js_ctx_shadowBlur_get;
           set_fn.setter = js_ctx_shadowBlur_set;
           JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
              JS_NewAtom(m_jsContext, "shadowBlur"),
              JS_NewCFunction2(m_jsContext, get_fn.generic, "get shadowBlur", 0, JS_CFUNC_getter, 0),
              JS_NewCFunction2(m_jsContext, set_fn.generic, "set shadowBlur", 1, JS_CFUNC_setter, 0),
              JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
           
           get_fn.getter = js_ctx_shadowColor_get;
           set_fn.setter = js_ctx_shadowColor_set;
           JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
              JS_NewAtom(m_jsContext, "shadowColor"),
              JS_NewCFunction2(m_jsContext, get_fn.generic, "get shadowColor", 0, JS_CFUNC_getter, 0),
              JS_NewCFunction2(m_jsContext, set_fn.generic, "set shadowColor", 1, JS_CFUNC_setter, 0),
              JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
           
           get_fn.getter = js_ctx_imageSmoothingEnabled_get;
           set_fn.setter = js_ctx_imageSmoothingEnabled_set;
           JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
              JS_NewAtom(m_jsContext, "imageSmoothingEnabled"),
              JS_NewCFunction2(m_jsContext, get_fn.generic, "get imageSmoothingEnabled", 0, JS_CFUNC_getter, 0),
              JS_NewCFunction2(m_jsContext, set_fn.generic, "set imageSmoothingEnabled", 1, JS_CFUNC_setter, 0),
              JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
           
           get_fn.getter = js_ctx_imageSmoothingQuality_get;
           set_fn.setter = js_ctx_imageSmoothingQuality_set;
           JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
              JS_NewAtom(m_jsContext, "imageSmoothingQuality"),
              JS_NewCFunction2(m_jsContext, get_fn.generic, "get imageSmoothingQuality", 0, JS_CFUNC_getter, 0),
              JS_NewCFunction2(m_jsContext, set_fn.generic, "set imageSmoothingQuality", 1, JS_CFUNC_setter, 0),
              JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
           
           // canvas property (getter only)
           get_fn.getter = js_ctx_canvas_get;
           JS_DefinePropertyGetSet(m_jsContext, ctx_proto,
              JS_NewAtom(m_jsContext, "canvas"),
              JS_NewCFunction2(m_jsContext, get_fn.generic, "get canvas", 0, JS_CFUNC_getter, 0),
              JS_UNDEFINED,
              JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        }
        
        JS_SetClassProto(m_jsContext, s_ctx_class_id, ctx_proto);
      }
      
      // === ImageData class prototype ===
      {
         JSValue img_proto = JS_NewObject(m_jsContext);
         
         JSCFunctionType get_fn;
         get_fn.getter = js_imagedata_width_get;
         JS_DefinePropertyGetSet(m_jsContext, img_proto,
            JS_NewAtom(m_jsContext, "width"),
            JS_NewCFunction2(m_jsContext, get_fn.generic, "get width", 0, JS_CFUNC_getter, 0),
            JS_UNDEFINED,
            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
         
         get_fn.getter = js_imagedata_height_get;
         JS_DefinePropertyGetSet(m_jsContext, img_proto,
            JS_NewAtom(m_jsContext, "height"),
            JS_NewCFunction2(m_jsContext, get_fn.generic, "get height", 0, JS_CFUNC_getter, 0),
            JS_UNDEFINED,
            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
         
         JS_SetClassProto(m_jsContext, s_imagedata_class_id, img_proto);
      }
      
      JS_FreeValue(m_jsContext, global);
   }
   
    Painter::~Painter()
    {
       // Clean up JS event listeners before freeing context/runtime
       if (m_jsContext)
       {
          JSValue global = JS_GetGlobalObject(m_jsContext);

          JSValue cleanupFn = JS_GetPropertyStr(m_jsContext, global, "cleanupEventListeners");
          if (JS_IsFunction(m_jsContext, cleanupFn))
          {
             JSValue result = JS_Call(m_jsContext, cleanupFn, JS_UNDEFINED, 0, nullptr);
             JS_FreeValue(m_jsContext, result);
          }
          JS_FreeValue(m_jsContext, cleanupFn);

          JS_FreeValue(m_jsContext, global);
       }

       delete m_script;
       delete m_context;
       
        // Free callback entries before freeing context
         m_callbacks.clear();
         
         // Free rAF callback entries
         for (auto& entry : m_rAFCallbacks)
            JS_FreeValue(m_jsContext, entry.second);
         m_rAFCallbacks.clear();
        
         // Free registered / auto-loaded images
         for (auto& entry : m_images)
            delete entry.second;
         m_images.clear();

         if (m_runtime)
            JS_RunGC(m_runtime);

         if (m_jsContext)
         {
            JS_FreeValue(m_jsContext, m_canvasElement);
            JS_FreeContext(m_jsContext);
            m_jsContext = 0;
         }
         if (m_runtime)
         {
            JS_RunGC(m_runtime);
            JS_FreeRuntime(m_runtime);
            m_runtime = 0;
         }
    }
   
   void Painter::setSize(int width, int height)
   {
      ScopedLock lock(m_painterMutex);
      m_width = width;
      m_height = height;
      if (m_context)
         m_context->setSize(width, height);
      dispatchResizeEvent();
   }
   
   void Painter::setWidth(int)
   {
      assert(0 && "Invalid operation, not allowed to set width!");
   }
         
   void Painter::setHeight(int)
   {
      assert(0 && "Invalid operation, not allowed to set height!");
   }
   
   void Painter::dispatchResizeEvent()
   {
      if (!m_jsContext) return;
      
      JSValue global = JS_GetGlobalObject(m_jsContext);
      JSValue window = JS_GetPropertyStr(m_jsContext, global, "window");
      if (JS_IsUndefined(window) || JS_IsNull(window))
      {
         JS_FreeValue(m_jsContext, window);
         JS_FreeValue(m_jsContext, global);
         return;
      }
      
      JSValue dispatchFn = JS_GetPropertyStr(m_jsContext, window, "dispatchEvent");
      if (JS_IsFunction(m_jsContext, dispatchFn))
      {
         JSValue eventObj = JS_NewObject(m_jsContext);
         JS_SetPropertyStr(m_jsContext, eventObj, "type", JS_NewString(m_jsContext, "resize"));
         JS_SetPropertyStr(m_jsContext, eventObj, "bubbles", JS_NewBool(m_jsContext, false));
         JS_SetPropertyStr(m_jsContext, eventObj, "cancelable", JS_NewBool(m_jsContext, false));
         
         JSValue argv[3];
         argv[0] = window;
         argv[1] = JS_NewString(m_jsContext, "resize");
         argv[2] = eventObj;
         
         JSValue result = JS_Call(m_jsContext, dispatchFn, JS_UNDEFINED, 3, argv);
         if (JS_IsException(result))
         {
            JSValue exception = JS_GetException(m_jsContext);
            const char* msg = JS_ToCString(m_jsContext, exception);
            std::cerr << "dispatchResizeEvent error: " << (msg ? msg : "unknown") << std::endl;
            if (msg) JS_FreeCString(m_jsContext, msg);
            JS_FreeValue(m_jsContext, exception);
         }
         JS_FreeValue(m_jsContext, result);
         JS_FreeValue(m_jsContext, argv[1]);
         JS_FreeValue(m_jsContext, eventObj);
      }
      else
      {
         JS_FreeValue(m_jsContext, dispatchFn);
      }
      
      JS_FreeValue(m_jsContext, window);
      JS_FreeValue(m_jsContext, global);
   }
   
   void Painter::dispatchVisibilityChangeEvent(bool hidden)
   {
      if (!m_jsContext) return;

      JSValue global = JS_GetGlobalObject(m_jsContext);
      JSValue document = JS_GetPropertyStr(m_jsContext, global, "document");
      if (JS_IsUndefined(document) || JS_IsNull(document))
      {
         JS_FreeValue(m_jsContext, document);
         JS_FreeValue(m_jsContext, global);
         return;
      }

      JS_SetPropertyStr(m_jsContext, document, "hidden", JS_NewBool(m_jsContext, hidden));
      JS_SetPropertyStr(m_jsContext, document, "visibilityState",
         JS_NewString(m_jsContext, hidden ? "hidden" : "visible"));

      JSValue dispatchFn = JS_GetPropertyStr(m_jsContext, document, "dispatchEvent");
      if (JS_IsFunction(m_jsContext, dispatchFn))
      {
         JSValue eventObj = JS_NewObject(m_jsContext);
         JS_SetPropertyStr(m_jsContext, eventObj, "type", JS_NewString(m_jsContext, "visibilitychange"));
         JS_SetPropertyStr(m_jsContext, eventObj, "bubbles", JS_NewBool(m_jsContext, false));
         JS_SetPropertyStr(m_jsContext, eventObj, "cancelable", JS_NewBool(m_jsContext, false));

         JSValue argv[3];
         argv[0] = document;
         argv[1] = JS_NewString(m_jsContext, "visibilitychange");
         argv[2] = eventObj;

         JSValue result = JS_Call(m_jsContext, dispatchFn, JS_UNDEFINED, 3, argv);
         if (JS_IsException(result))
         {
            JSValue exception = JS_GetException(m_jsContext);
            const char* msg = JS_ToCString(m_jsContext, exception);
            std::cerr << "dispatchVisibilityChangeEvent error: " << (msg ? msg : "unknown") << std::endl;
            if (msg) JS_FreeCString(m_jsContext, msg);
            JS_FreeValue(m_jsContext, exception);
         }
         JS_FreeValue(m_jsContext, result);
         JS_FreeValue(m_jsContext, argv[1]);
         JS_FreeValue(m_jsContext, eventObj);
      }
      else
      {
         JS_FreeValue(m_jsContext, dispatchFn);
      }

      JS_FreeValue(m_jsContext, document);
      JS_FreeValue(m_jsContext, global);
   }

   void Painter::dispatchWindowFocusEvent(bool focused)
   {
      if (!m_jsContext) return;

      const char* eventType = focused ? "focus" : "blur";

      JSValue global = JS_GetGlobalObject(m_jsContext);
      JSValue window = JS_GetPropertyStr(m_jsContext, global, "window");
      if (JS_IsUndefined(window) || JS_IsNull(window))
      {
         JS_FreeValue(m_jsContext, window);
         JS_FreeValue(m_jsContext, global);
         return;
      }

      JSValue dispatchFn = JS_GetPropertyStr(m_jsContext, window, "dispatchEvent");
      if (JS_IsFunction(m_jsContext, dispatchFn))
      {
         JSValue eventObj = JS_NewObject(m_jsContext);
         JS_SetPropertyStr(m_jsContext, eventObj, "type", JS_NewString(m_jsContext, eventType));
         JS_SetPropertyStr(m_jsContext, eventObj, "bubbles", JS_NewBool(m_jsContext, false));
         JS_SetPropertyStr(m_jsContext, eventObj, "cancelable", JS_NewBool(m_jsContext, false));

         JSValue argv[3];
         argv[0] = window;
         argv[1] = JS_NewString(m_jsContext, eventType);
         argv[2] = eventObj;

         JSValue result = JS_Call(m_jsContext, dispatchFn, JS_UNDEFINED, 3, argv);
         if (JS_IsException(result))
         {
            JSValue exception = JS_GetException(m_jsContext);
            const char* msg = JS_ToCString(m_jsContext, exception);
            std::cerr << "dispatchWindowFocusEvent error: " << (msg ? msg : "unknown") << std::endl;
            if (msg) JS_FreeCString(m_jsContext, msg);
            JS_FreeValue(m_jsContext, exception);
         }
         JS_FreeValue(m_jsContext, result);
         JS_FreeValue(m_jsContext, argv[1]);
         JS_FreeValue(m_jsContext, eventObj);
      }
      else
      {
         JS_FreeValue(m_jsContext, dispatchFn);
      }

      JS_FreeValue(m_jsContext, window);
      JS_FreeValue(m_jsContext, global);
   }

   void Painter::draw()
   {
      ScopedLock lock(m_painterMutex);
      
      static int drawCount = 0;
      drawCount++;
      if (drawCount <= 5 || drawCount % 50 == 0)
      {
         std::cerr << "[Painter::draw] #" << drawCount;
         std::cerr << " callbacks=" << m_callbacks.size();
         if (m_runtime) std::cerr << " jobsPending=" << JS_IsJobPending(m_runtime);
         std::cerr << std::endl;
      }

      // Process pending microtasks (Promise continuations, async function resumes)
      if (m_runtime)
      {
         while (JS_IsJobPending(m_runtime))
         {
            JSContext *pctx = nullptr;
            int ret = JS_ExecutePendingJob(m_runtime, &pctx);
            if (ret < 0)
            {
               JSContext *errCtx = pctx ? pctx : m_jsContext;
               JSValue exception = JS_GetException(errCtx);
               const char * msg = JS_ToCString(errCtx, exception);
               const char * stack = nullptr;
               JSValue stackVal = JS_GetPropertyStr(errCtx, exception, "stack");
               if (!JS_IsUndefined(stackVal))
                  stack = JS_ToCString(errCtx, stackVal);
               std::cerr << "[Job Error] " << (msg ? msg : "unknown") << std::endl;
               if (stack) std::cerr << "[Job Error] " << stack << std::endl;
               JS_FreeValue(errCtx, stackVal);
               if (msg) JS_FreeCString(errCtx, msg);
               if (stack) JS_FreeCString(errCtx, stack);
               JS_FreeValue(errCtx, exception);
               break;
            }
            if (ret == 0) break;
         }
      }
      
       // Fire rAF callbacks (one-shot, every frame, with timestamp)
       if (!m_rAFCallbacks.empty())
       {
          double rAfTime = m_rafTimer.seconds() * 1000.0;
          std::vector<int> rAfKeys;
          rAfKeys.reserve(m_rAFCallbacks.size());
          for (auto& entry : m_rAFCallbacks)
             rAfKeys.push_back(entry.first);
          
           for (int id : rAfKeys)
           {
              auto it = m_rAFCallbacks.find(id);
              if (it == m_rAFCallbacks.end()) continue;

              JSValue func = it->second;
              m_rAFCallbacks.erase(it);

              JSValue ts = JS_NewFloat64(m_jsContext, rAfTime);
              JSValue result = JS_Call(m_jsContext, func, JS_UNDEFINED, 1, &ts);
              JS_FreeValue(m_jsContext, ts);
              if (JS_IsException(result))
              {
                 JSValue exception = JS_GetException(m_jsContext);
                 const char* msg = JS_ToCString(m_jsContext, exception);
                 std::cerr << "rAF error: " << (msg ? msg : "unknown") << std::endl;
                 if (msg) JS_FreeCString(m_jsContext, msg);
                 JS_FreeValue(m_jsContext, exception);
              }
              JS_FreeValue(m_jsContext, result);
              JS_FreeValue(m_jsContext, func);
           }
       }
       
       // Collect keys first to avoid iterator invalidation
       // (callbacks may call clearInterval during execution)
       std::vector<int> keys;
      keys.reserve(m_callbacks.size());
      for (CallbackMap::const_iterator it = m_callbacks.begin();
           it != m_callbacks.end(); ++it)
         keys.push_back(it->first);
      
      double dt = m_timer.seconds();
      m_timer.reset();
      
      for (std::vector<int>::const_iterator kit = keys.begin();
           kit != keys.end(); ++kit)
      {
         CallbackMap::iterator it = m_callbacks.find(*kit);
         if (it == m_callbacks.end())
            continue; // was cleared during a previous callback
         
           Callback &callback = it->second;
           
           bool wasOneShot = callback.oneShot();
           JSValue result = callback.call(dt);
           if (wasOneShot)
              m_callbacks.erase(it);
         if (JS_IsException(result))
         {
            JSValue exception = JS_GetException(m_jsContext);
            const char * msg = JS_ToCString(m_jsContext, exception);
            std::cerr << "Script runtime error: " << (msg ? msg : "unknown") << std::endl;
            if (msg) JS_FreeCString(m_jsContext, msg);
            JS_FreeValue(m_jsContext, exception);
         }
         JS_FreeValue(m_jsContext, result);
      }
   }
   
   void Painter::copyImageTo(void * target)
   {
      ScopedLock lock(m_painterMutex);
      if (target && m_context)
         m_context->copyImageTo(target);
   }
   
   int Painter::setInterval(JSValue function, int interval)
   {
      m_callbacks.insert(std::make_pair(++m_callbackIndex, Callback(m_jsContext, function, interval)));
      return m_callbackIndex;
   }
   
   void Painter::clearInterval(int index)
   {
      CallbackMap::iterator result = m_callbacks.find(index);
      if (result != m_callbacks.end())
         m_callbacks.erase(result);
   }

   int Painter::setTimeout(JSValue function, int delay)
   {
      m_callbacks.insert(std::make_pair(++m_callbackIndex, Callback(m_jsContext, function, delay, true)));
      return m_callbackIndex;
   }

   void Painter::clearTimeout(int id)
   {
      clearInterval(id);
   }
   
   JSValue Painter::getContext(std::string const& type)
   {
      if (type == "2d")
      {
         JSValue obj = JS_NewObjectClass(m_jsContext, s_ctx_class_id);
         JS_SetOpaque(obj, m_context);
         return obj;
      }
      
      std::cerr << "Error: Requested wrong context type '" << type << "'" << std::endl;
      return JS_UNDEFINED;
   }
   
   void Painter::registerImage(std::string const& name, ImageData * image)
   {
      assert(image);
      ScopedLock lock(m_imageMutex);
      
      std::map<std::string, ImageData*>::iterator result = m_images.find(name);
      if (result != m_images.end())
      {
         std::cerr << "Failed to register image '" << name << "'. Image with the same name present!" << std::endl;
         return;
      }
      
      m_images.insert(std::make_pair(name, image));
   }
   
   JSValue Painter::getImage(std::string const& name)
   {
      ScopedLock lock(m_imageMutex);
      
      std::map<std::string, ImageData*>::iterator result = m_images.find(name);
      if (result == m_images.end())
         return JS_UNDEFINED;
         
      JSValue obj = JS_NewObjectClass(m_jsContext, s_imagedata_class_id);
      JS_SetOpaque(obj, result->second);
      return obj;
   }
   
   void Painter::log(std::string const& log)
   {
      ScopedLock lock(m_logMutex);
      
      m_history.push_back(log);
      
      // We only store a 1000 log entries
      if (m_history.size() > 1000)
         m_history.pop_front();
   }
   
   std::string Painter::lastLogEntry()
   {
      ScopedLock lock(m_logMutex);
      
      if (m_history.empty())
         return "";
      
      std::string log = m_history.front();
      m_history.pop_front();
      return log;
   }
   
   void Painter::dispatchMouseEvent(const char* type, int x, int y, int button)
   {
      ScopedLock lock(m_painterMutex);
      if (!m_jsContext) return;
      
      JSValue canvasEl = JS_DupValue(m_jsContext, m_canvasElement);
      if (JS_IsUndefined(canvasEl) || JS_IsNull(canvasEl))
      {
         JS_FreeValue(m_jsContext, canvasEl);
         return;
      }
      
      JSValue global = JS_GetGlobalObject(m_jsContext);
      JSValue window = JS_GetPropertyStr(m_jsContext, global, "window");
      
      if (!JS_IsUndefined(window) && !JS_IsNull(window))
      {
         JSValue createFn = JS_GetPropertyStr(m_jsContext, window, "createMouseEvent");
         if (JS_IsFunction(m_jsContext, createFn))
         {
            JSValue args[3];
            args[0] = JS_NewInt32(m_jsContext, x);
            args[1] = JS_NewInt32(m_jsContext, y);
            args[2] = JS_NewInt32(m_jsContext, button);
            JSValue eventObj = JS_Call(m_jsContext, createFn, window, 3, args);
            JS_FreeValue(m_jsContext, args[0]);
            JS_FreeValue(m_jsContext, args[1]);
            JS_FreeValue(m_jsContext, args[2]);
            JS_FreeValue(m_jsContext, createFn);
            
            if (!JS_IsException(eventObj))
            {
               JSValue dispatchFn = JS_GetPropertyStr(m_jsContext, window, "dispatchEvent");
               if (JS_IsFunction(m_jsContext, dispatchFn))
               {
                  JSValue argv[3];
                  argv[0] = canvasEl;
                  argv[1] = JS_NewString(m_jsContext, type);
                  argv[2] = eventObj;
                  
                  JSValue result = JS_Call(m_jsContext, dispatchFn, window, 3, argv);
                  if (JS_IsException(result))
                  {
                     JSValue exception = JS_GetException(m_jsContext);
                     const char* msg = JS_ToCString(m_jsContext, exception);
                     std::cerr << "dispatchMouseEvent error: " << (msg ? msg : "unknown") << std::endl;
                     if (msg) JS_FreeCString(m_jsContext, msg);
                     JS_FreeValue(m_jsContext, exception);
                  }
                  JS_FreeValue(m_jsContext, result);
                  JS_FreeValue(m_jsContext, argv[1]);
                  JS_FreeValue(m_jsContext, eventObj);
               }
               else
               {
                  JS_FreeValue(m_jsContext, dispatchFn);
                  JS_FreeValue(m_jsContext, eventObj);
               }
               JS_FreeValue(m_jsContext, dispatchFn);
            }
            else
            {
               JS_FreeValue(m_jsContext, eventObj);
            }
         }
         else
         {
            JS_FreeValue(m_jsContext, createFn);
         }
      }
      
      JS_FreeValue(m_jsContext, window);
      JS_FreeValue(m_jsContext, global);
      JS_FreeValue(m_jsContext, canvasEl);
   }
   
   void Painter::dispatchWheelEvent(const char* type, int x, int y, int deltaY, bool ctrlKey, bool shiftKey, bool altKey)
   {
      ScopedLock lock(m_painterMutex);
      if (!m_jsContext) return;
      
      JSValue canvasEl = JS_DupValue(m_jsContext, m_canvasElement);
      if (JS_IsUndefined(canvasEl) || JS_IsNull(canvasEl))
      {
         JS_FreeValue(m_jsContext, canvasEl);
         return;
      }
      
      JSValue global = JS_GetGlobalObject(m_jsContext);
      JSValue window = JS_GetPropertyStr(m_jsContext, global, "window");
      
      if (!JS_IsUndefined(window) && !JS_IsNull(window))
      {
         JSValue createFn = JS_GetPropertyStr(m_jsContext, window, "createWheelEvent");
         if (!JS_IsFunction(m_jsContext, createFn))
         {
            JS_FreeValue(m_jsContext, createFn);
            JS_FreeValue(m_jsContext, window);
            JS_FreeValue(m_jsContext, global);
            JS_FreeValue(m_jsContext, canvasEl);
            return;
         }
         
         JSValue args[6];
         args[0] = JS_NewInt32(m_jsContext, x);
         args[1] = JS_NewInt32(m_jsContext, y);
         args[2] = JS_NewInt32(m_jsContext, deltaY);
         args[3] = JS_NewBool(m_jsContext, ctrlKey);
         args[4] = JS_NewBool(m_jsContext, shiftKey);
         args[5] = JS_NewBool(m_jsContext, altKey);
         JSValue eventObj = JS_Call(m_jsContext, createFn, window, 6, args);
         JS_FreeValue(m_jsContext, args[0]);
         JS_FreeValue(m_jsContext, args[1]);
         JS_FreeValue(m_jsContext, args[2]);
         JS_FreeValue(m_jsContext, args[3]);
         JS_FreeValue(m_jsContext, args[4]);
         JS_FreeValue(m_jsContext, args[5]);
         JS_FreeValue(m_jsContext, createFn);
         
         if (!JS_IsException(eventObj))
         {
            JSValue dispatchFn = JS_GetPropertyStr(m_jsContext, window, "dispatchEvent");
            if (JS_IsFunction(m_jsContext, dispatchFn))
            {
               JSValue argv[3];
               argv[0] = canvasEl;
               argv[1] = JS_NewString(m_jsContext, type);
               argv[2] = eventObj;
               
               JSValue result = JS_Call(m_jsContext, dispatchFn, window, 3, argv);
               if (JS_IsException(result))
               {
                  JSValue exception = JS_GetException(m_jsContext);
                  const char* msg = JS_ToCString(m_jsContext, exception);
                  std::cerr << "dispatchWheelEvent error: " << (msg ? msg : "unknown") << std::endl;
                  if (msg) JS_FreeCString(m_jsContext, msg);
                  JS_FreeValue(m_jsContext, exception);
               }
               JS_FreeValue(m_jsContext, result);
                   JS_FreeValue(m_jsContext, argv[1]);
                   JS_FreeValue(m_jsContext, eventObj);
                }
                else
                {
                   JS_FreeValue(m_jsContext, dispatchFn);
                   JS_FreeValue(m_jsContext, eventObj);
                }
                JS_FreeValue(m_jsContext, dispatchFn);
             }
         else
         {
            JS_FreeValue(m_jsContext, eventObj);
         }
      }
      
      JS_FreeValue(m_jsContext, window);
      JS_FreeValue(m_jsContext, global);
      JS_FreeValue(m_jsContext, canvasEl);
   }
   
   void Painter::dispatchKeyEvent(const char* type, const char* key, bool ctrlKey, bool shiftKey, bool altKey)
   {
      ScopedLock lock(m_painterMutex);
      if (!m_jsContext) return;
      
      JSValue global = JS_GetGlobalObject(m_jsContext);
      JSValue document = JS_GetPropertyStr(m_jsContext, global, "document");
      JSValue window = JS_GetPropertyStr(m_jsContext, global, "window");
      
      if (!JS_IsUndefined(document) && !JS_IsNull(document)
         && !JS_IsUndefined(window) && !JS_IsNull(window))
      {
         JSValue createFn = JS_GetPropertyStr(m_jsContext, window, "createKeyEvent");
         if (JS_IsFunction(m_jsContext, createFn))
         {
            JSValue args[4];
            args[0] = JS_NewString(m_jsContext, key);
            args[1] = JS_NewBool(m_jsContext, ctrlKey);
            args[2] = JS_NewBool(m_jsContext, shiftKey);
            args[3] = JS_NewBool(m_jsContext, altKey);
            JSValue eventObj = JS_Call(m_jsContext, createFn, window, 4, args);
            JS_FreeValue(m_jsContext, args[0]);
            JS_FreeValue(m_jsContext, args[1]);
            JS_FreeValue(m_jsContext, args[2]);
            JS_FreeValue(m_jsContext, args[3]);
            JS_FreeValue(m_jsContext, createFn);
            
            if (!JS_IsException(eventObj))
            {
               JSValue dispatchFn = JS_GetPropertyStr(m_jsContext, window, "dispatchEvent");
               if (JS_IsFunction(m_jsContext, dispatchFn))
               {
                  JSValue argv[3];
                  argv[0] = document;
                  argv[1] = JS_NewString(m_jsContext, type);
                  argv[2] = eventObj;
                  
                  JSValue result = JS_Call(m_jsContext, dispatchFn, window, 3, argv);
                  if (JS_IsException(result))
                  {
                     JSValue exception = JS_GetException(m_jsContext);
                     const char* msg = JS_ToCString(m_jsContext, exception);
                     std::cerr << "dispatchKeyEvent error: " << (msg ? msg : "unknown") << std::endl;
                     if (msg) JS_FreeCString(m_jsContext, msg);
                     JS_FreeValue(m_jsContext, exception);
                  }
                   JS_FreeValue(m_jsContext, result);
                   JS_FreeValue(m_jsContext, argv[1]);
                   JS_FreeValue(m_jsContext, eventObj);
                }else
                {
                   JS_FreeValue(m_jsContext, dispatchFn);
                   JS_FreeValue(m_jsContext, eventObj);
                }
                JS_FreeValue(m_jsContext, dispatchFn);
             }
             else
             {
                JS_FreeValue(m_jsContext, eventObj);
             }
          }
          else
          {
             JS_FreeValue(m_jsContext, createFn);
          }
       }
       
       JS_FreeValue(m_jsContext, window);
       JS_FreeValue(m_jsContext, document);
       JS_FreeValue(m_jsContext, global);
    }
    
    // =========================================================================
    // Static QuickJS callback implementations
    // =========================================================================
   
   // --- Window methods ---
   
   JSValue Painter::js_setInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || argc < 1 || !JS_IsFunction(ctx, argv[0]))
         return JS_ThrowTypeError(ctx, "setInterval requires a function");
      
      int interval = 0;
      if (argc >= 2)
         JS_ToInt32(ctx, &interval, argv[1]);
      
      int id = self->setInterval(argv[0], interval);
      return JS_NewInt32(ctx, id);
   }
   
   JSValue Painter::js_clearInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || argc < 1)
         return JS_UNDEFINED;
      
      int index;
      JS_ToInt32(ctx, &index, argv[0]);
      self->clearInterval(index);
      return JS_UNDEFINED;
   }

   JSValue Painter::js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || argc < 1 || !JS_IsFunction(ctx, argv[0]))
         return JS_ThrowTypeError(ctx, "setTimeout requires a function");

      int delay = 0;
      if (argc >= 2)
         JS_ToInt32(ctx, &delay, argv[1]);

      int id = self->setTimeout(argv[0], delay);
      return JS_NewInt32(ctx, id);
   }

   JSValue Painter::js_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || argc < 1)
         return JS_UNDEFINED;

      int id;
      JS_ToInt32(ctx, &id, argv[0]);
      self->clearTimeout(id);
      return JS_UNDEFINED;
   }
   
   int Painter::requestAnimationFrame(JSValue function)
   {
      JSValue dup = JS_DupValue(m_jsContext, function);
      m_rAFCallbacks.insert(std::make_pair(++m_rafIndex, dup));
      return m_rafIndex;
   }
   
   void Painter::cancelAnimationFrame(int id)
   {
      auto it = m_rAFCallbacks.find(id);
      if (it != m_rAFCallbacks.end())
      {
         JS_FreeValue(m_jsContext, it->second);
         m_rAFCallbacks.erase(it);
      }
   }
   
   JSValue Painter::js_requestAnimationFrame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || argc < 1 || !JS_IsFunction(ctx, argv[0]))
         return JS_ThrowTypeError(ctx, "requestAnimationFrame requires a function");
      
      int id = self->requestAnimationFrame(argv[0]);
      return JS_NewInt32(ctx, id);
   }
   
   JSValue Painter::js_cancelAnimationFrame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || argc < 1)
         return JS_UNDEFINED;
      
      int id;
      JS_ToInt32(ctx, &id, argv[0]);
      self->cancelAnimationFrame(id);
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_getContext(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || argc < 1)
         return JS_UNDEFINED;
      
      const char * type = JS_ToCString(ctx, argv[0]);
      if (!type)
         return JS_UNDEFINED;
      
      JSValue result = self->getContext(type);
      JS_FreeCString(ctx, type);
      return result;
   }
   
   JSValue Painter::js_getImage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || argc < 1)
         return JS_UNDEFINED;
      
      const char * name = JS_ToCString(ctx, argv[0]);
      if (!name)
         return JS_UNDEFINED;
      
      JSValue result = self->getImage(name);
      JS_FreeCString(ctx, name);
      return result;
   }
   
   JSValue Painter::js_setCanvasElementRef(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || argc < 1)
         return JS_UNDEFINED;
      
      JS_FreeValue(ctx, self->m_canvasElement);
      self->m_canvasElement = JS_DupValue(ctx, argv[0]);
      return JS_UNDEFINED;
   }
   
   static bool isAbsolutePath(const std::string& p)
   {
      if (p.empty()) return false;
      if (p[0] == '/') return true;
#ifdef _WIN32
      if (p.size() >= 2 && isalpha((unsigned char)p[0]) && p[1] == ':') return true;
      if (p[0] == '\\') return true;
#endif
      return false;
   }

   static std::string resolvePath(const std::string& base, const std::string& relative)
   {
      if (base.empty() || isAbsolutePath(relative)) return relative;
      if (relative.empty()) return base;
      std::string result = base;
      if (!result.empty() && result.back() != '/' && result.back() != '\\')
         result += '/';
      result += relative;
      return result;
   }

   JSValue Painter::js_readFile(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      if (argc < 1) return JS_UNDEFINED;

      const char * path = JS_ToCString(ctx, argv[0]);
      if (!path) return JS_UNDEFINED;

      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));

      std::string resolvedPath;
      if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
      {
         const char * baseDir = JS_ToCString(ctx, argv[1]);
         if (baseDir)
         {
            resolvedPath = resolvePath(baseDir, path);
            JS_FreeCString(ctx, baseDir);
         }
         else
         {
            resolvedPath = path;
         }
      }
      else if (self && !self->m_entryDir.empty())
      {
         resolvedPath = resolvePath(self->m_entryDir, path);
      }
      else
      {
         resolvedPath = path;
      }

      JS_FreeCString(ctx, path);

      std::string content = ScriptLoader::readFile(resolvedPath);

      if (content.empty())
         return JS_NULL;

      return JS_NewStringLen(ctx, content.c_str(), content.size());
   }

   static ImageData* loadImageFromFile(const std::string& resolvedPath)
   {
      cairo_surface_t* surface = cairo_image_surface_create_from_png(resolvedPath.c_str());
      cairo_status_t status = cairo_surface_status(surface);
      if (status == CAIRO_STATUS_SUCCESS)
      {
         return new ImageData(surface);
      }
      cairo_surface_destroy(surface);

      // Try with SDL_image if available (for JPEG, GIF, etc.)
#ifdef CANVAS_HAS_SDL_IMAGE
      SDL_Surface* sdlSurface = IMG_Load(resolvedPath.c_str());
      if (sdlSurface)
      {
         int w = sdlSurface->w;
         int h = sdlSurface->h;
         ImageData* img = new ImageData(sdlSurface->pixels, w, h, Canvas::kRGBA);
         SDL_FreeSurface(sdlSurface);
         return img;
      }
#endif
      return nullptr;
   }

   JSValue Painter::js_loadImage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      if (argc < 1) return JS_NULL;

      const char* path = JS_ToCString(ctx, argv[0]);
      if (!path) return JS_NULL;

      Painter* self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      std::string key(path);
      JS_FreeCString(ctx, path);

      if (self)
      {
         ImageData* img = nullptr;

         // Auto-load from disk (resolve path like JS module)
         if (!self->m_entryDir.empty())
         {
            std::string resolvedPath = resolvePath(self->m_entryDir, key);
            img = loadImageFromFile(resolvedPath);
         }
         if (!img)
            img = loadImageFromFile(key);

         if (img)
         {
            // Cache for future lookups
            self->m_images[key] = img;

            JSValue result = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, result, "width", JS_NewInt32(ctx, img->width()));
            JS_SetPropertyStr(ctx, result, "height", JS_NewInt32(ctx, img->height()));
            return result;
         }
      }
      return JS_NULL;
   }

   static const char* getMimeType(const std::string& path)
   {
      size_t dot = path.find_last_of('.');
      if (dot == std::string::npos) return "application/octet-stream";
      std::string ext = path.substr(dot);
      for (auto& c : ext) c = tolower(c);
      if (ext == ".html" || ext == ".htm") return "text/html";
      if (ext == ".js" || ext == ".mjs") return "text/javascript";
      if (ext == ".json") return "application/json";
      if (ext == ".png") return "image/png";
      if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
      if (ext == ".gif") return "image/gif";
      if (ext == ".svg") return "image/svg+xml";
      if (ext == ".css") return "text/css";
      if (ext == ".txt") return "text/plain";
      if (ext == ".xml") return "application/xml";
      if (ext == ".wasm") return "application/wasm";
      if (ext == ".ico") return "image/x-icon";
      if (ext == ".woff") return "font/woff";
      if (ext == ".woff2") return "font/woff2";
      if (ext == ".ttf") return "font/ttf";
      return "application/octet-stream";
   }

   JSValue Painter::js_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      if (argc < 1)
         return JS_ThrowTypeError(ctx, "__fetch requires at least 1 argument");

      const char* url = JS_ToCString(ctx, argv[0]);
      if (!url)
         return JS_ThrowTypeError(ctx, "__fetch: invalid URL");

      Painter* self = static_cast<Painter*>(JS_GetContextOpaque(ctx));

      // Resolve path relative to entry directory
      std::string resolvedPath;
      if (self && !self->m_entryDir.empty())
         resolvedPath = resolvePath(self->m_entryDir, url);
      else
         resolvedPath = url;

      JS_FreeCString(ctx, url);

      // Read file content as string
      std::string content = ScriptLoader::readFile(resolvedPath);

      bool ok = !content.empty();
      int status = ok ? 200 : 404;
      const char* statusText = ok ? "OK" : "Not Found";
      const char* mime = ok ? getMimeType(resolvedPath) : "text/plain";

      // Build result object
      JSValue result = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, result, "ok", JS_NewBool(ctx, ok));
      JS_SetPropertyStr(ctx, result, "status", JS_NewInt32(ctx, status));
      JS_SetPropertyStr(ctx, result, "statusText", JS_NewString(ctx, statusText));
      JS_SetPropertyStr(ctx, result, "url", JS_NewString(ctx, resolvedPath.c_str()));
      JS_SetPropertyStr(ctx, result, "contentType", JS_NewString(ctx, mime));
      if (ok)
         JS_SetPropertyStr(ctx, result, "body", JS_NewStringLen(ctx, content.c_str(), content.size()));
      else
         JS_SetPropertyStr(ctx, result, "body", JS_NULL);

      return result;
   }

   // Write callback for cairo_surface_write_to_png_stream
   static cairo_status_t png_write_callback(void *closure, const unsigned char *data, unsigned int length)
   {
      std::vector<unsigned char> *buffer = static_cast<std::vector<unsigned char>*>(closure);
      buffer->insert(buffer->end(), data, data + length);
      return CAIRO_STATUS_SUCCESS;
   }
   
   static std::string base64_encode(const unsigned char *data, size_t len)
   {
      static const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string result;
      result.reserve((len + 2) / 3 * 4);
      
      for (size_t i = 0; i < len; i += 3)
      {
         unsigned char b0 = data[i];
         unsigned char b1 = (i + 1 < len) ? data[i + 1] : 0;
         unsigned char b2 = (i + 2 < len) ? data[i + 2] : 0;
         
         result += chars[b0 >> 2];
         result += chars[((b0 & 0x03) << 4) | (b1 >> 4)];
         result += (i + 1 < len) ? chars[((b1 & 0x0f) << 2) | (b2 >> 6)] : '=';
         result += (i + 2 < len) ? chars[b2 & 0x3f] : '=';
      }
      return result;
   }
   
   JSValue Painter::js_canvasToDataURL(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self || !self->m_context)
         return JS_NewString(ctx, "data:image/png,");
      
      cairo_surface_t *surface = self->m_context->getSurface();
      if (!surface)
         return JS_NewString(ctx, "data:image/png,");
      
      std::vector<unsigned char> pngData;
      cairo_status_t status = cairo_surface_write_to_png_stream(surface, png_write_callback, &pngData);
      
      if (status != CAIRO_STATUS_SUCCESS || pngData.empty())
         return JS_NewString(ctx, "data:image/png,");
      
      std::string encoded = base64_encode(pngData.data(), pngData.size());
      std::string dataUrl = std::string("data:image/png;base64,") + encoded;
      
      return JS_NewStringLen(ctx, dataUrl.c_str(), dataUrl.size());
   }
   
   JSValue Painter::js_window_width_get(JSContext *ctx, JSValueConst this_val)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self)
         return JS_UNDEFINED;
      return JS_NewInt32(ctx, self->width());
   }
   
   JSValue Painter::js_window_height_get(JSContext *ctx, JSValueConst this_val)
   {
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (!self)
         return JS_UNDEFINED;
      return JS_NewInt32(ctx, self->height());
   }
   
   // --- Console ---
   
JSValue Painter::js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
   Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
   if (!self || argc < 1)
      return JS_UNDEFINED;
   
   std::string msg;
   for (int i = 0; i < argc; i++)
   {
      if (i > 0) msg += " ";
      const char * str = JS_ToCString(ctx, argv[i]);
      if (str)
      {
         msg += str;
         JS_FreeCString(ctx, str);
      }
      else
      {
         msg += "undefined";
      }
   }
   self->log(msg);
   return JS_UNDEFINED;
}

JSValue Painter::js_debug(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
   if (argc < 1) return JS_UNDEFINED;
   const char * str = JS_ToCString(ctx, argv[0]);
   if (str)
   {
      fprintf(stderr, "[DEBUG] %s\n", str);
      JS_FreeCString(ctx, str);
   }
   return JS_UNDEFINED;
}
   
   // --- Context2D methods ---
   
   JSValue Painter::js_ctx_fillRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 4) return JS_UNDEFINED;
      static int fillRectCount = 0;
      if (fillRectCount < 5) {
         fprintf(stderr, "[fillRect] #%d args=(%f,%f,%f,%f)\n", fillRectCount,
            jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
            jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]));
         fillRectCount++;
      }
      c->fillRect(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
                   jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_strokeRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 4) return JS_UNDEFINED;
      c->strokeRect(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
                     jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_clearRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 4) return JS_UNDEFINED;
      c->clearRect(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
                    jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->clear();
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_fill(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->fill();
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_stroke(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->stroke();
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_beginPath(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->beginPath();
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_closePath(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->closePath();
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_moveTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 2) return JS_UNDEFINED;
      c->moveTo(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_lineTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 2) return JS_UNDEFINED;
      c->lineTo(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_quadraticCurveTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 4) return JS_UNDEFINED;
      c->quadraticCurveTo(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
                           jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_bezierCurveTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 6) return JS_UNDEFINED;
      c->bezierCurveTo(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
                        jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]),
                        jsval_to_float(ctx, argv[4]), jsval_to_float(ctx, argv[5]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_arcTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 5) return JS_UNDEFINED;
      c->arcTo(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
               jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]),
               jsval_to_float(ctx, argv[4]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 4) return JS_UNDEFINED;
      c->rect(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
              jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_scale(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 2) return JS_UNDEFINED;
      c->scale(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_rotate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 1) return JS_UNDEFINED;
      c->rotate(jsval_to_float(ctx, argv[0]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_translate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 2) return JS_UNDEFINED;
      c->translate(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_drawImage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 3) return JS_UNDEFINED;
      
      ImageData * image = static_cast<ImageData*>(JS_GetOpaque(argv[0], s_imagedata_class_id));
      if (!image)
      {
         // Check if it's an Image element: look up by src in m_images
         JSValue srcVal = JS_GetPropertyStr(ctx, argv[0], "_src");
         if (!JS_IsUndefined(srcVal) && !JS_IsNull(srcVal))
         {
            const char* src = JS_ToCString(ctx, srcVal);
            if (src)
            {
               Painter* self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
               if (self)
               {
                  auto it = self->m_images.find(std::string(src));
                  if (it != self->m_images.end())
                     image = it->second;
               }
               JS_FreeCString(ctx, src);
            }
         }
         JS_FreeValue(ctx, srcVal);
      }

      if (!image)
         return JS_ThrowTypeError(ctx, "drawImage: first argument must be an ImageData or Image");
      
      float x = jsval_to_float(ctx, argv[1]);
      float y = jsval_to_float(ctx, argv[2]);
      float w = (argc >= 5) ? jsval_to_float(ctx, argv[3]) : static_cast<float>(image->width());
      float h = (argc >= 5) ? jsval_to_float(ctx, argv[4]) : static_cast<float>(image->height());
      
      c->drawImage(image, x, y, w, h);
      return JS_UNDEFINED;
   }
   
   // --- Context2D property getters/setters ---
   
   JSValue Painter::js_ctx_lineWidth_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      return JS_NewFloat64(ctx, c->lineWidth());
   }
   
   JSValue Painter::js_ctx_lineWidth_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->setLineWidth(jsval_to_float(ctx, val));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_lineCap_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      std::string cap = c->lineCap();
      return JS_NewStringLen(ctx, cap.c_str(), cap.size());
   }
   
   JSValue Painter::js_ctx_lineCap_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      const char * str = JS_ToCString(ctx, val);
      if (str)
      {
         c->setLineCap(str);
         JS_FreeCString(ctx, str);
      }
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_strokeStyle_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      std::string style = c->strokeStyle();
      return JS_NewStringLen(ctx, style.c_str(), style.size());
   }
   
   JSValue Painter::js_ctx_fillStyle_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      std::string style = c->fillStyle();
      return JS_NewStringLen(ctx, style.c_str(), style.size());
   }
   
   JSValue Painter::js_ctx_globalAlpha_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      return JS_NewFloat64(ctx, c->globalAlpha());
   }
   
   JSValue Painter::js_ctx_globalAlpha_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->setGlobalAlpha(jsval_to_float(ctx, val));
      return JS_UNDEFINED;
   }
   
   // --- New Context2D method callbacks ---
   
   JSValue Painter::js_ctx_save(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->save();
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_restore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->restore();
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_setTransform(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 6) return JS_UNDEFINED;
      c->setTransform(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
                       jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]),
                       jsval_to_float(ctx, argv[4]), jsval_to_float(ctx, argv[5]));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_resetTransform(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->resetTransform();
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_arc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 5) return JS_UNDEFINED;
      bool ccw = (argc >= 6) ? JS_ToBool(ctx, argv[5]) : false;
      c->arc(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
             jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]),
             jsval_to_float(ctx, argv[4]), ccw);
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_ellipse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 7) return JS_UNDEFINED;
      bool ccw = (argc >= 8) ? JS_ToBool(ctx, argv[7]) : false;
      c->ellipse(jsval_to_float(ctx, argv[0]), jsval_to_float(ctx, argv[1]),
                 jsval_to_float(ctx, argv[2]), jsval_to_float(ctx, argv[3]),
                 jsval_to_float(ctx, argv[4]), jsval_to_float(ctx, argv[5]),
                 jsval_to_float(ctx, argv[6]), ccw);
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_clip(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->clip();
      return JS_UNDEFINED;
   }
   
   // --- Text methods ---
   
   JSValue Painter::js_ctx_fillText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 3) return JS_UNDEFINED;
      const char * text = JS_ToCString(ctx, argv[0]);
      if (!text) return JS_UNDEFINED;
      c->fillText(text, jsval_to_float(ctx, argv[1]), jsval_to_float(ctx, argv[2]));
      JS_FreeCString(ctx, text);
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_strokeText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 3) return JS_UNDEFINED;
      const char * text = JS_ToCString(ctx, argv[0]);
      if (!text) return JS_UNDEFINED;
      c->strokeText(text, jsval_to_float(ctx, argv[1]), jsval_to_float(ctx, argv[2]));
      JS_FreeCString(ctx, text);
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_measureText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c || argc < 1) return JS_UNDEFINED;
      const char * text = JS_ToCString(ctx, argv[0]);
      if (!text) return JS_UNDEFINED;
      
      double width = 0, ascent = 0, descent = 0;
      c->measureText(text, width, ascent, descent);
      JS_FreeCString(ctx, text);
      
      JSValue result = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, result, "width", JS_NewFloat64(ctx, width));
      return result;
   }
   
   // --- Gradient methods ---
   
   // Helper to parse a CSS color string to RGBA components
   static bool parseColorString(JSContext *ctx, const char * str, float &r, float &g, float &b, float &a)
   {
      // Simple color parsing - handle rgb(), rgba(), hex, and named colors
      std::string s(str);
      a = 1.0f;
      
      if (s.empty()) return false;
      
      // Handle rgba(r,g,b,a) or rgb(r,g,b)
      if (s.substr(0, 4) == "rgba")
      {
         int ri, gi, bi;
         float aa;
         if (sscanf(s.c_str(), "rgba(%d,%d,%d,%f)", &ri, &gi, &bi, &aa) == 4)
         {
            r = ri / 255.0f; g = gi / 255.0f; b = bi / 255.0f; a = aa;
            return true;
         }
         // Try with spaces
         if (sscanf(s.c_str(), "rgba(%d ,%d ,%d ,%f)", &ri, &gi, &bi, &aa) == 4)
         {
            r = ri / 255.0f; g = gi / 255.0f; b = bi / 255.0f; a = aa;
            return true;
         }
      }
      else if (s.substr(0, 3) == "rgb")
      {
         int ri, gi, bi;
         if (sscanf(s.c_str(), "rgb(%d,%d,%d)", &ri, &gi, &bi) == 3)
         {
            r = ri / 255.0f; g = gi / 255.0f; b = bi / 255.0f;
            return true;
         }
      }
      
      // Handle hex colors
      if (s[0] == '#')
      {
         unsigned int hex = 0;
         if (s.length() == 4) // #RGB
         {
            unsigned int rv, gv, bv;
            sscanf(s.c_str(), "#%1x%1x%1x", &rv, &gv, &bv);
            r = rv / 15.0f; g = gv / 15.0f; b = bv / 15.0f;
            return true;
         }
         else if (s.length() == 7) // #RRGGBB
         {
            sscanf(s.c_str() + 1, "%x", &hex);
            r = ((hex >> 16) & 0xFF) / 255.0f;
            g = ((hex >> 8) & 0xFF) / 255.0f;
            b = (hex & 0xFF) / 255.0f;
            return true;
         }
         else if (s.length() == 9) // #RRGGBBAA
         {
            sscanf(s.c_str() + 1, "%x", &hex);
            r = ((hex >> 24) & 0xFF) / 255.0f;
            g = ((hex >> 16) & 0xFF) / 255.0f;
            b = ((hex >> 8) & 0xFF) / 255.0f;
            a = (hex & 0xFF) / 255.0f;
            return true;
         }
      }
      
      // Named colors (basic set)
      if (s == "black") { r = 0; g = 0; b = 0; return true; }
      if (s == "white") { r = 1; g = 1; b = 1; return true; }
      if (s == "red") { r = 1; g = 0; b = 0; return true; }
      if (s == "green") { r = 0; g = 0.5f; b = 0; return true; }
      if (s == "blue") { r = 0; g = 0; b = 1; return true; }
      if (s == "transparent") { r = 0; g = 0; b = 0; a = 0; return true; }
      
      return false;
   }
   
   // Helper to read gradient stops from a JS object's "stops" array
   static void readGradientData(JSContext *ctx, JSValueConst gradObj, canvas::GradientData &data)
   {
      // Read type
      JSValue typeVal = JS_GetPropertyStr(ctx, gradObj, "_type");
      const char * typeStr = JS_ToCString(ctx, typeVal);
      if (typeStr)
      {
         if (strcmp(typeStr, "linear") == 0) data.type = canvas::GradientData::LINEAR;
         else if (strcmp(typeStr, "radial") == 0) data.type = canvas::GradientData::RADIAL;
         JS_FreeCString(ctx, typeStr);
      }
      JS_FreeValue(ctx, typeVal);
      
      // Read coordinates
      JSValue val;
      val = JS_GetPropertyStr(ctx, gradObj, "_x0"); jsval_to_float_ptr(ctx, &data.x0, val); JS_FreeValue(ctx, val);
      val = JS_GetPropertyStr(ctx, gradObj, "_y0"); jsval_to_float_ptr(ctx, &data.y0, val); JS_FreeValue(ctx, val);
      val = JS_GetPropertyStr(ctx, gradObj, "_x1"); jsval_to_float_ptr(ctx, &data.x1, val); JS_FreeValue(ctx, val);
      val = JS_GetPropertyStr(ctx, gradObj, "_y1"); jsval_to_float_ptr(ctx, &data.y1, val); JS_FreeValue(ctx, val);
      val = JS_GetPropertyStr(ctx, gradObj, "_r0"); jsval_to_float_ptr(ctx, &data.r0, val); JS_FreeValue(ctx, val);
      val = JS_GetPropertyStr(ctx, gradObj, "_r1"); jsval_to_float_ptr(ctx, &data.r1, val); JS_FreeValue(ctx, val);
      
      // Read stops array
      JSValue stopsArr = JS_GetPropertyStr(ctx, gradObj, "_stops");
      if (!JS_IsUndefined(stopsArr) && !JS_IsNull(stopsArr))
      {
         JSValue lenVal = JS_GetPropertyStr(ctx, stopsArr, "length");
         int len = 0;
         JS_ToInt32(ctx, &len, lenVal);
         JS_FreeValue(ctx, lenVal);
         
         for (int i = 0; i < len; i++)
         {
            JSValue stop = JS_GetPropertyUint32(ctx, stopsArr, i);
            if (JS_IsUndefined(stop) || JS_IsNull(stop)) continue;
            
            JSValue offsetVal = JS_GetPropertyStr(ctx, stop, "offset");
            JSValue colorVal = JS_GetPropertyStr(ctx, stop, "color");
            
            canvas::GradientStop gs;
            jsval_to_float_ptr(ctx, &gs.offset, offsetVal);
            
            const char * colorStr = JS_ToCString(ctx, colorVal);
            if (colorStr)
            {
               float cr, cg, cb, ca;
               if (parseColorString(ctx, colorStr, cr, cg, cb, ca))
                  gs.color = canvas::Color(cr, cg, cb, ca);
               JS_FreeCString(ctx, colorStr);
            }
            
            data.stops.push_back(gs);
            
            JS_FreeValue(ctx, offsetVal);
            JS_FreeValue(ctx, colorVal);
            JS_FreeValue(ctx, stop);
         }
      }
      JS_FreeValue(ctx, stopsArr);
   }
   
   JSValue Painter::js_ctx_createLinearGradient(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      if (argc < 4) return JS_UNDEFINED;
      
      JSValue grad = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, grad, "_type", JS_NewString(ctx, "linear"));
      JS_SetPropertyStr(ctx, grad, "_x0", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[0])));
      JS_SetPropertyStr(ctx, grad, "_y0", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[1])));
      JS_SetPropertyStr(ctx, grad, "_x1", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[2])));
      JS_SetPropertyStr(ctx, grad, "_y1", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[3])));
      JS_SetPropertyStr(ctx, grad, "_r0", JS_NewFloat64(ctx, 0));
      JS_SetPropertyStr(ctx, grad, "_r1", JS_NewFloat64(ctx, 0));
      JS_SetPropertyStr(ctx, grad, "_stops", JS_NewArray(ctx));
      
      // Add addColorStop method
      JS_SetPropertyStr(ctx, grad, "addColorStop",
         JS_NewCFunction(ctx, js_gradient_addColorStop, "addColorStop", 2));
      
      return grad;
   }
   
   JSValue Painter::js_ctx_createRadialGradient(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      if (argc < 6) return JS_UNDEFINED;
      
      JSValue grad = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, grad, "_type", JS_NewString(ctx, "radial"));
      JS_SetPropertyStr(ctx, grad, "_x0", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[0])));
      JS_SetPropertyStr(ctx, grad, "_y0", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[1])));
      JS_SetPropertyStr(ctx, grad, "_r0", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[2])));
      JS_SetPropertyStr(ctx, grad, "_x1", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[3])));
      JS_SetPropertyStr(ctx, grad, "_y1", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[4])));
      JS_SetPropertyStr(ctx, grad, "_r1", JS_NewFloat64(ctx, jsval_to_float(ctx, argv[5])));
      JS_SetPropertyStr(ctx, grad, "_stops", JS_NewArray(ctx));
      
      // Add addColorStop method
      JS_SetPropertyStr(ctx, grad, "addColorStop",
         JS_NewCFunction(ctx, js_gradient_addColorStop, "addColorStop", 2));
      
      return grad;
   }
   
   JSValue Painter::js_gradient_addColorStop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
      if (argc < 2) return JS_UNDEFINED;
      
      JSValue stopsArr = JS_GetPropertyStr(ctx, this_val, "_stops");
      if (JS_IsUndefined(stopsArr) || JS_IsNull(stopsArr))
      {
         JS_FreeValue(ctx, stopsArr);
         return JS_UNDEFINED;
      }
      
      JSValue stop = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, stop, "offset", JS_DupValue(ctx, argv[0]));
      JS_SetPropertyStr(ctx, stop, "color", JS_DupValue(ctx, argv[1]));
      
      // Push to array
      JSValue pushFn = JS_GetPropertyStr(ctx, stopsArr, "push");
      if (JS_IsFunction(ctx, pushFn))
      {
         JS_Call(ctx, pushFn, stopsArr, 1, &stop);
      }
      JS_FreeValue(ctx, pushFn);
      JS_FreeValue(ctx, stopsArr);
      
      return JS_UNDEFINED;
   }
   
   // --- Fill/Stroke style setters with gradient support ---
   
   // Override the fillStyle setter to handle both strings and gradient objects
   JSValue Painter::js_ctx_fillStyle_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      
      static int fsCount = 0;
      if (JS_IsString(val) && fsCount < 5) {
         const char * s = JS_ToCString(ctx, val);
         fprintf(stderr, "[fillStyle] #%d = '%s'\n", fsCount, s ? s : "?");
         if (s) JS_FreeCString(ctx, s);
         fsCount++;
      }
      
      if (JS_IsString(val))
      {
         const char * str = JS_ToCString(ctx, val);
         if (str)
         {
            c->setFillStyle(str);
            JS_FreeCString(ctx, str);
         }
      }
      else if (JS_IsObject(val))
      {
         // Check if this is a gradient object
         JSValue typeVal = JS_GetPropertyStr(ctx, val, "_type");
         if (!JS_IsUndefined(typeVal) && !JS_IsNull(typeVal))
         {
            canvas::GradientData data;
            readGradientData(ctx, val, data);
            c->setFillGradient(data);
         }
         JS_FreeValue(ctx, typeVal);
      }
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_strokeStyle_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      
      if (JS_IsString(val))
      {
         const char * str = JS_ToCString(ctx, val);
         if (str)
         {
            c->setStrokeStyle(str);
            JS_FreeCString(ctx, str);
         }
      }
      else if (JS_IsObject(val))
      {
         // Check if this is a gradient object
         JSValue typeVal = JS_GetPropertyStr(ctx, val, "_type");
         if (!JS_IsUndefined(typeVal) && !JS_IsNull(typeVal))
         {
            canvas::GradientData data;
            readGradientData(ctx, val, data);
            c->setStrokeGradient(data);
         }
         JS_FreeValue(ctx, typeVal);
      }
      return JS_UNDEFINED;
   }
   
   // --- New property getters/setters ---
   
   JSValue Painter::js_ctx_font_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      std::string f = c->font();
      return JS_NewStringLen(ctx, f.c_str(), f.size());
   }
   
   JSValue Painter::js_ctx_font_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      const char * str = JS_ToCString(ctx, val);
      if (str)
      {
         c->setFont(str);
         JS_FreeCString(ctx, str);
      }
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_textAlign_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      std::string s = c->textAlign();
      return JS_NewStringLen(ctx, s.c_str(), s.size());
   }
   
   JSValue Painter::js_ctx_textAlign_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      const char * str = JS_ToCString(ctx, val);
      if (str)
      {
         c->setTextAlign(str);
         JS_FreeCString(ctx, str);
      }
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_textBaseline_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      std::string s = c->textBaseline();
      return JS_NewStringLen(ctx, s.c_str(), s.size());
   }
   
   JSValue Painter::js_ctx_textBaseline_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      const char * str = JS_ToCString(ctx, val);
      if (str)
      {
         c->setTextBaseline(str);
         JS_FreeCString(ctx, str);
      }
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_globalCompositeOperation_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      std::string s = c->globalCompositeOperation();
      return JS_NewStringLen(ctx, s.c_str(), s.size());
   }
   
   JSValue Painter::js_ctx_globalCompositeOperation_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      const char * str = JS_ToCString(ctx, val);
      if (str)
      {
         c->setGlobalCompositeOperation(str);
         JS_FreeCString(ctx, str);
      }
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_shadowBlur_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      return JS_NewFloat64(ctx, c->shadowBlur());
   }
   
   JSValue Painter::js_ctx_shadowBlur_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->setShadowBlur(jsval_to_float(ctx, val));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_shadowColor_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      std::string s = c->shadowColor();
      return JS_NewStringLen(ctx, s.c_str(), s.size());
   }
   
   JSValue Painter::js_ctx_shadowColor_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      const char * str = JS_ToCString(ctx, val);
      if (str)
      {
         c->setShadowColor(str);
         JS_FreeCString(ctx, str);
      }
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_imageSmoothingEnabled_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      return JS_NewBool(ctx, c->imageSmoothingEnabled());
   }
   
   JSValue Painter::js_ctx_imageSmoothingEnabled_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      c->setImageSmoothingEnabled(JS_ToBool(ctx, val));
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_imageSmoothingQuality_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      std::string s = c->imageSmoothingQuality();
      return JS_NewStringLen(ctx, s.c_str(), s.size());
   }
   
   JSValue Painter::js_ctx_imageSmoothingQuality_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      const char * str = JS_ToCString(ctx, val);
      if (str)
      {
         c->setImageSmoothingQuality(str);
         JS_FreeCString(ctx, str);
      }
      return JS_UNDEFINED;
   }
   
   JSValue Painter::js_ctx_canvas_get(JSContext *ctx, JSValueConst this_val)
   {
      Context * c = static_cast<Context*>(JS_GetOpaque2(ctx, this_val, s_ctx_class_id));
      if (!c) return JS_UNDEFINED;
      
      Painter * self = static_cast<Painter*>(JS_GetContextOpaque(ctx));
      if (self && !JS_IsUndefined(self->m_canvasElement) && !JS_IsNull(self->m_canvasElement))
         return JS_DupValue(ctx, self->m_canvasElement);
      
      JSValue canvas = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, canvas, "width", JS_NewInt32(ctx, c->canvasWidth()));
      JS_SetPropertyStr(ctx, canvas, "height", JS_NewInt32(ctx, c->canvasHeight()));
      return canvas;
   }
   
   // --- ImageData property getters ---
   
   JSValue Painter::js_imagedata_width_get(JSContext *ctx, JSValueConst this_val)
   {
      ImageData * img = static_cast<ImageData*>(JS_GetOpaque2(ctx, this_val, s_imagedata_class_id));
      if (!img) return JS_UNDEFINED;
      return JS_NewInt32(ctx, img->width());
   }
   
    JSValue Painter::js_imagedata_height_get(JSContext *ctx, JSValueConst this_val)
   {
       ImageData * img = static_cast<ImageData*>(JS_GetOpaque2(ctx, this_val, s_imagedata_class_id));
       if (!img) return JS_UNDEFINED;
       return JS_NewInt32(ctx, img->height());
   }
   
   // =========================================================================
   // Native event listener implementations
   // =========================================================================
   
   static JSValue js_event_noop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
       return JS_UNDEFINED;
   }
   
   JSValue Painter::js_addEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
       if (argc < 2) return JS_UNDEFINED;
       
       const char * eventName = JS_ToCString(ctx, argv[0]);
       if (!eventName) return JS_UNDEFINED;
       
       if (!JS_IsFunction(ctx, argv[1]))
       {
           JS_FreeCString(ctx, eventName);
           return JS_UNDEFINED;
       }
       
       JSValue events = JS_GetPropertyStr(ctx, this_val, "_events");
       if (!JS_IsObject(events))
       {
           JS_FreeValue(ctx, events);
           events = JS_NewObject(ctx);
           JS_SetPropertyStr(ctx, this_val, "_events", JS_DupValue(ctx, events));
       }
       
       JSValue listeners = JS_GetPropertyStr(ctx, events, eventName);
       if (!JS_IsArray(ctx, listeners))
       {
           JS_FreeValue(ctx, listeners);
           listeners = JS_NewArray(ctx);
           JS_SetPropertyStr(ctx, events, eventName, JS_DupValue(ctx, listeners));
       }
       
       JSValue pushFn = JS_GetPropertyStr(ctx, listeners, "push");
       if (JS_IsFunction(ctx, pushFn))
       {
           JS_Call(ctx, pushFn, listeners, 1, &argv[1]);
       }
       JS_FreeValue(ctx, pushFn);
       JS_FreeValue(ctx, listeners);
       JS_FreeValue(ctx, events);
       JS_FreeCString(ctx, eventName);
       
       return JS_UNDEFINED;
   }
   
   JSValue Painter::js_removeEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
       if (argc < 2) return JS_UNDEFINED;
       
       const char * eventName = JS_ToCString(ctx, argv[0]);
       if (!eventName) return JS_UNDEFINED;
       
       JSValue events = JS_GetPropertyStr(ctx, this_val, "_events");
       if (!JS_IsObject(events))
       {
           JS_FreeValue(ctx, events);
           JS_FreeCString(ctx, eventName);
           return JS_UNDEFINED;
       }
       
       JSValue listeners = JS_GetPropertyStr(ctx, events, eventName);
       if (JS_IsArray(ctx, listeners))
       {
           JSValue indexOfFn = JS_GetPropertyStr(ctx, listeners, "indexOf");
           if (JS_IsFunction(ctx, indexOfFn))
           {
               JSValue idxVal = JS_Call(ctx, indexOfFn, listeners, 1, &argv[1]);
               int idx;
               JS_ToInt32(ctx, &idx, idxVal);
               JS_FreeValue(ctx, idxVal);
               
               if (idx >= 0)
               {
                   JSValue spliceFn = JS_GetPropertyStr(ctx, listeners, "splice");
                   if (JS_IsFunction(ctx, spliceFn))
                   {
                       JSValue spliceArgs[2];
                       spliceArgs[0] = JS_NewInt32(ctx, idx);
                       spliceArgs[1] = JS_NewInt32(ctx, 1);
                       JSValue result = JS_Call(ctx, spliceFn, listeners, 2, spliceArgs);
                       JS_FreeValue(ctx, result);
                       JS_FreeValue(ctx, spliceArgs[0]);
                       JS_FreeValue(ctx, spliceArgs[1]);
                   }
                   JS_FreeValue(ctx, spliceFn);
               }
           }
           JS_FreeValue(ctx, indexOfFn);
       }
       JS_FreeValue(ctx, listeners);
       JS_FreeValue(ctx, events);
       JS_FreeCString(ctx, eventName);
       
       return JS_UNDEFINED;
   }
   
    JSValue Painter::js_dispatchEvent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
    {
        if (argc < 1) return JS_UNDEFINED;
        
        JSValue target;
        const char * eventName;
        JSValue eventObj;
        
        if (argc >= 3)
        {
            // Internal convention: dispatchEvent(target, eventName, eventObj)
            target = argv[0];
            eventName = JS_ToCString(ctx, argv[1]);
            if (!eventName) return JS_UNDEFINED;
            eventObj = argv[2];
        }
        else
        {
            // Standard convention: element.dispatchEvent(event)
            target = this_val;
            eventObj = argv[0];
            JSValue typeVal = JS_GetPropertyStr(ctx, eventObj, "type");
            eventName = JS_IsString(typeVal) ? JS_ToCString(ctx, typeVal) : nullptr;
            JS_FreeValue(ctx, typeVal);
            if (!eventName) return JS_UNDEFINED;
        }
        
        JSValue events = JS_GetPropertyStr(ctx, target, "_events");
       if (JS_IsObject(events))
       {
           JSValue listeners = JS_GetPropertyStr(ctx, events, eventName);
           if (JS_IsArray(ctx, listeners))
           {
               JSValue lenVal = JS_GetPropertyStr(ctx, listeners, "length");
               int len;
               JS_ToInt32(ctx, &len, lenVal);
               JS_FreeValue(ctx, lenVal);
               
               for (int i = 0; i < len; i++)
               {
                   JSValue fn = JS_GetPropertyUint32(ctx, listeners, i);
                   if (JS_IsFunction(ctx, fn))
                   {
                       JSValue result = JS_Call(ctx, fn, target, 1, &eventObj);
                       if (JS_IsException(result))
                       {
                           JSValue exception = JS_GetException(ctx);
                           const char* msg = JS_ToCString(ctx, exception);
                           std::cerr << "dispatchEvent error: " << (msg ? msg : "unknown") << std::endl;
                           if (msg) JS_FreeCString(ctx, msg);
                           JS_FreeValue(ctx, exception);
                       }
                       JS_FreeValue(ctx, result);
                   }
                   JS_FreeValue(ctx, fn);
               }
           }
           JS_FreeValue(ctx, listeners);
       }
       JS_FreeValue(ctx, events);
       JS_FreeCString(ctx, eventName);
       
       return JS_UNDEFINED;
   }
   
    static void deleteProp(JSContext *ctx, JSValueConst obj, const char* name)
    {
        JSAtom atom = JS_NewAtom(ctx, name);
        JS_DeleteProperty(ctx, obj, atom, JS_PROP_THROW);
        JS_FreeAtom(ctx, atom);
    }

    JSValue Painter::js_cleanupEventListeners(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
    {
        JSValue global = JS_GetGlobalObject(ctx);
        
        JSValue window = JS_GetPropertyStr(ctx, global, "window");
        if (JS_IsObject(window))
        {
            JS_SetPropertyStr(ctx, window, "_events", JS_NewObject(ctx));
        }
        JS_FreeValue(ctx, window);
        
        JSValue doc = JS_GetPropertyStr(ctx, global, "document");
        if (JS_IsObject(doc))
        {
            JS_SetPropertyStr(ctx, doc, "_events", JS_NewObject(ctx));
        }
        JS_FreeValue(ctx, doc);
        
        JS_SetPropertyStr(ctx, global, "_events", JS_NewObject(ctx));
        
        JS_FreeValue(ctx, global);
        return JS_UNDEFINED;
    }
   
   // =========================================================================
   // Native event factory implementations
   // =========================================================================
   
   JSValue Painter::js_createKeyEvent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
       JSValue obj = JS_NewObject(ctx);
       JS_SetPropertyStr(ctx, obj, "key", (argc >= 1) ? JS_DupValue(ctx, argv[0]) : JS_NewString(ctx, ""));
       JS_SetPropertyStr(ctx, obj, "ctrlKey", JS_NewBool(ctx, argc >= 2 && JS_ToBool(ctx, argv[1])));
       JS_SetPropertyStr(ctx, obj, "shiftKey", JS_NewBool(ctx, argc >= 3 && JS_ToBool(ctx, argv[2])));
       JS_SetPropertyStr(ctx, obj, "altKey", JS_NewBool(ctx, argc >= 4 && JS_ToBool(ctx, argv[3])));
       JS_SetPropertyStr(ctx, obj, "preventDefault", JS_NewCFunction(ctx, js_event_noop, "preventDefault", 0));
       JS_SetPropertyStr(ctx, obj, "stopPropagation", JS_NewCFunction(ctx, js_event_noop, "stopPropagation", 0));
       return obj;
   }
   
   JSValue Painter::js_createMouseEvent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
       JSValue obj = JS_NewObject(ctx);
       JS_SetPropertyStr(ctx, obj, "clientX", (argc >= 1) ? JS_DupValue(ctx, argv[0]) : JS_NewInt32(ctx, 0));
       JS_SetPropertyStr(ctx, obj, "clientY", (argc >= 2) ? JS_DupValue(ctx, argv[1]) : JS_NewInt32(ctx, 0));
       JS_SetPropertyStr(ctx, obj, "button", (argc >= 3) ? JS_DupValue(ctx, argv[2]) : JS_NewInt32(ctx, 0));
       JS_SetPropertyStr(ctx, obj, "ctrlKey", JS_NewBool(ctx, false));
       JS_SetPropertyStr(ctx, obj, "shiftKey", JS_NewBool(ctx, false));
       JS_SetPropertyStr(ctx, obj, "altKey", JS_NewBool(ctx, false));
       JS_SetPropertyStr(ctx, obj, "preventDefault", JS_NewCFunction(ctx, js_event_noop, "preventDefault", 0));
       JS_SetPropertyStr(ctx, obj, "stopPropagation", JS_NewCFunction(ctx, js_event_noop, "stopPropagation", 0));
       return obj;
   }
   
   JSValue Painter::js_createWheelEvent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
       JSValue obj = JS_NewObject(ctx);
       JS_SetPropertyStr(ctx, obj, "clientX", (argc >= 1) ? JS_DupValue(ctx, argv[0]) : JS_NewInt32(ctx, 0));
       JS_SetPropertyStr(ctx, obj, "clientY", (argc >= 2) ? JS_DupValue(ctx, argv[1]) : JS_NewInt32(ctx, 0));
       JS_SetPropertyStr(ctx, obj, "deltaY", (argc >= 3) ? JS_DupValue(ctx, argv[2]) : JS_NewInt32(ctx, 0));
       JS_SetPropertyStr(ctx, obj, "ctrlKey", JS_NewBool(ctx, argc >= 4 && JS_ToBool(ctx, argv[3])));
       JS_SetPropertyStr(ctx, obj, "shiftKey", JS_NewBool(ctx, argc >= 5 && JS_ToBool(ctx, argv[4])));
       JS_SetPropertyStr(ctx, obj, "altKey", JS_NewBool(ctx, argc >= 6 && JS_ToBool(ctx, argv[5])));
       JS_SetPropertyStr(ctx, obj, "preventDefault", JS_NewCFunction(ctx, js_event_noop, "preventDefault", 0));
       JS_SetPropertyStr(ctx, obj, "stopPropagation", JS_NewCFunction(ctx, js_event_noop, "stopPropagation", 0));
       return obj;
   }
   
   JSValue Painter::js_createTouchEvent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
   {
       JSValue obj = JS_NewObject(ctx);
       
       JSValue touch = JS_NewObject(ctx);
       JS_SetPropertyStr(ctx, touch, "clientX", (argc >= 1) ? JS_DupValue(ctx, argv[0]) : JS_NewInt32(ctx, 0));
       JS_SetPropertyStr(ctx, touch, "clientY", (argc >= 2) ? JS_DupValue(ctx, argv[1]) : JS_NewInt32(ctx, 0));
       
       JSValue touches = JS_NewArray(ctx);
       JS_SetPropertyUint32(ctx, touches, 0, JS_DupValue(ctx, touch));
       JS_SetPropertyStr(ctx, obj, "touches", touches);
       
       JSValue changedTouches = JS_NewArray(ctx);
       JS_SetPropertyUint32(ctx, changedTouches, 0, touch);
       JS_SetPropertyStr(ctx, obj, "changedTouches", changedTouches);
       
       JS_SetPropertyStr(ctx, obj, "preventDefault", JS_NewCFunction(ctx, js_event_noop, "preventDefault", 0));
       JS_SetPropertyStr(ctx, obj, "stopPropagation", JS_NewCFunction(ctx, js_event_noop, "stopPropagation", 0));
       return obj;
   }
   
}