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

#include "Script.h"
#include "EventClasses.h"
#include <Log.h>
#include "Context.h"
#include "ImageData.h"
#include "Orientation.h"
#include "quickjs-libc.h"
#include "cutils.h"
#include <chrono>
#include <fstream>
#include <string>
#include <vector>
#include "browser_shim.h"
#include "Bindings.h"
#include "Style.h"
#include "quickjs.h"
#include "util/JSError.h"
#include "algorithm"
namespace canvas
{
   static int started = 0;

   Script::Script(JSContext * context)
      : m_context(context),
        m_runtime(JS_GetRuntime(context)),
        m_rAFCallbacks(),
        m_timerCallbacks(),
        m_absoluteTimer(),
        m_protos(),
        m_window(new WindowClass())
   {
     JS_SetModuleLoaderFunc2(m_runtime, NULL, js_module_loader,
                             js_module_check_attributes, NULL);
     
      registerBindings(context,m_protos);

      auto global = JS_GetGlobalObject(context);
      JS_SetContextOpaque(context, this);
      JS_SetOpaque(global, m_window);
      m_window->initJsFields(context, global);
      JS_FreeValue(context, global);

     std::string shimSource(BROWSER_SHIM_JS, BROWSER_SHIM_JS_LEN);
     if (!runString(shimSource, "browser_shim.js")) {
       LOGE("Failed to evaluate browser shim");
       return;
     } else {
       LOGI("Evaluted browser_shim.js.");
     }
   }

   Script::~Script() {
     for (auto& cb : m_timerCallbacks) {
      JS_FreeValue(m_context, cb.function);
    }
    m_timerCallbacks.clear();

    // 2. 释放所有 rAF 回调
    for (auto& pair : m_rAFCallbacks) {
      JS_FreeValue(m_context, pair.second);
    }
    m_rAFCallbacks.clear();

    JS_RunGC(m_runtime);

    // un register proto
    JSValue global = JS_GetGlobalObject(m_context);
    JS_SetPrototype(m_context,global , JS_NULL);
    m_window->freeJsFields(m_runtime);
    delete m_window;

     // Runtime/Context is owned by App, not Script
   }
    bool Script::tickScript(int iterations) {
        if (iterations < 0) iterations = 1;
        else if (iterations > 10) iterations = 10;

        bool didSomething = false;
        for (int i = 0; i < iterations; ++i) {
            if (m_runtime) JS_UpdateStackTop(m_runtime);

            bool executed = false;

            // 执行一个到期的定时器
            if (!m_timerCallbacks.empty()) {
                double nowSec = m_absoluteTimer.seconds();
                if (m_timerCallbacks.begin()->nextTime <= nowSec) {
                    if(executeOneTimer())
                      executed = true;
                }
            }

            // 执行所有微任务
            if (drainMicrotasks())
                executed = true;

            if (!executed)
                break;   // 没有可做的工作，提前退出
            didSomething = true;
        }
        return didSomething;
    }
    bool Script::executeRAFCallbacks() {
      if (!m_context)
        return false;

      double rAfTime = m_absoluteTimer.seconds() * 1000.0; // 毫秒

      // 复制当前 rAF 列表（因为执行过程中可能调用 cancel）
      std::vector<int> rAfKeys;
      for (auto &entry : m_rAFCallbacks)
        rAfKeys.push_back(entry.first);

       bool executed = false;
       for (int id : rAfKeys) {
         auto it = m_rAFCallbacks.find(id);
         if (it == m_rAFCallbacks.end())
           continue;

         executed = true;
         JSValue func = it->second;
         m_rAFCallbacks.erase(it); // 一次性 rAF，执行后移除

         JSValue ts = JS_NewFloat64(m_context, rAfTime);
         auto t0 = std::chrono::steady_clock::now();
         JSValue result = JS_Call(m_context, func, JS_UNDEFINED, 1, &ts);
         auto t1 = std::chrono::steady_clock::now();
         auto callUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        //  fprintf(stderr, "[RAF] cb_id=%d duration=%ldus\n", id, callUs);
         JS_FreeValue(m_context, ts);
         if (JS_IsException(result)) {
           JSValue exception = JS_GetException(m_context);
           printException(m_context, exception, "rAF error: ");
           JS_FreeValue(m_context, exception);
         }
         JS_FreeValue(m_context, result);
         JS_FreeValue(m_context, func);
       }
       return executed;
    }

   bool Script::executeOneTimer() {
     if (m_timerCallbacks.empty())
       return false;
     double nowSec = m_absoluteTimer.seconds();
     auto it = m_timerCallbacks.begin();
     if (it->nextTime > nowSec)
       return false;

     int id = it->id;
     bool isInterval = it->isInterval;
     int intervalMs = it->intervalMs;

     // 1. 独立 dup 一份函数，用于回调
     JSValue func = JS_DupValue(m_context, it->function);

     // 2. 执行回调
     JSValue result = JS_Call(m_context, func, JS_UNDEFINED, 0, nullptr);
     if (JS_IsException(result)) {
       JSValue ex = JS_GetException(m_context);
       printException(m_context, ex, "Timer callback error: ");
       JS_FreeValue(m_context, ex);
     }
     JS_FreeValue(m_context, result);

     // 3. 检查定时器是否还被存在
     auto found = std::find_if(m_timerCallbacks.begin(), m_timerCallbacks.end(),
                               [id](auto &cb) { return cb.id == id; });
     if (found == m_timerCallbacks.end()) {
       JS_FreeValue(m_context, func); // 已清除，释放 dup 副本
       return true;
     }

     // 4. 一次性：删除原条目，释放 dup
     if (!isInterval) {
       JS_FreeValue(m_context, found->function);
       m_timerCallbacks.erase(found);
       JS_FreeValue(m_context, func);
       return true;
     }

     // 5. 周期性：删除原条目，直接用 func 作为新条目的函数（不再额外 dup）
     JS_FreeValue(m_context, found->function);
     m_timerCallbacks.erase(found);
     double newTime = nowSec + intervalMs / 1000.0;
     TimerCallback newCb(id, func, intervalMs, true, newTime);
     m_timerCallbacks.insert(std::move(newCb));
     // 注意：这里没有释放 func，因为已经转移给新条目
     return true;
   }
   bool Script::drainMicrotasks() {
     if (!m_runtime)
       return false;

     while (JS_IsJobPending(m_runtime)) {
       JSContext *pctx = nullptr;
       int ret = JS_ExecutePendingJob(m_runtime, &pctx);
       if (ret < 0) {
         JSContext *errCtx = pctx ? pctx : m_context;
         JSValue exception = JS_GetException(errCtx);
         const char *msg = JS_ToCString(errCtx, exception);
         const char *stack = nullptr;
         JSValue stackVal = JS_GetPropertyStr(errCtx, exception, "stack");
         if (!JS_IsUndefined(stackVal))
           stack = JS_ToCString(errCtx, stackVal);
         LOGE("[Microtask Error] %s", msg ? msg : "unknown");
         if (stack)
           LOGE("%s", stack);
         JS_FreeValue(errCtx, stackVal);
         if (msg)
           JS_FreeCString(errCtx, msg);
         if (stack)
           JS_FreeCString(errCtx, stack);
         JS_FreeValue(errCtx, exception);
         return true;
       }
       if (ret == 0)
         return true;
     }
     return false;
   }
   JSValue Script::getObject(std::string const &name) {
     JSValue global = JS_GetGlobalObject(m_context);
     JSValue val = JS_GetPropertyStr(m_context, global, name.c_str());
     JS_FreeValue(m_context, global);
     return val;
   }

   bool Script::load(std::string const &filename) {
     std::vector<char> data;

     // Read file
     std::ifstream inf(filename.c_str(), std::ios::binary);
     if (!inf.good()) {
       inf.close();
       return false;
     }

     char ch;
     while (inf.get(ch))
       data.push_back(ch);

     inf.close();

     data.push_back('\0');

     return runString(std::string(&data[0]), filename);
   }

   bool Script::load(std::vector<std::string> const& files)
   {
      std::vector<char> data;
      
      // Load all the files
      for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++it)
      {
         std::string filename = *it;
      
         // Read file
         std::ifstream inf(filename.c_str(), std::ios::binary);
         if (inf.good())
         {
            // Read file
            char ch;
            while (inf.get(ch))
               data.push_back(ch);
         }
         else
            LOGE("Could not read file '%s'", filename.c_str());
         
         // Insert a newline between each file
         data.push_back('\n');
         inf.close();
      }
      
      data.push_back('\0');
      return runString(std::string(&data[0]), "");
   }
      
      
   bool Script::runModule(std::string const& code, std::string const& filename)
   {
      started++;
      const char * fname = filename.empty() ? "<module>" : filename.c_str();

      JSValue val = JS_Eval(m_context, code.c_str(), code.size(), fname,
                            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
      if (JS_IsException(val))
      {
         JSValue exception = JS_GetException(m_context);
         const char * msg = JS_ToCString(m_context, exception);
         const char * stack = nullptr;

         JSValue stackVal = JS_GetPropertyStr(m_context, exception, "stack");
         if (!JS_IsUndefined(stackVal))
            stack = JS_ToCString(m_context, stackVal);

          LOGE("+----");
          LOGE("| Module compile error in %s:", fname);
          LOGE("| %s", msg ? msg : "unknown error");
          if (stack)
             LOGE("| %s", stack);
          LOGE("+----");

          if (msg) JS_FreeCString(m_context, msg);
          if (stack) JS_FreeCString(m_context, stack);
          JS_FreeValue(m_context, stackVal);
          JS_FreeValue(m_context, exception);
          JS_FreeValue(m_context, val);
          return false;
       }

       js_module_set_import_meta(m_context, val, TRUE, TRUE);

       val = JS_EvalFunction(m_context, val);
       LOGI("[Script] runModule %s: eval complete", fname);
      if (JS_IsException(val))
      {
         JSValue exception = JS_GetException(m_context);
         const char * msg = JS_ToCString(m_context, exception);
         const char * stack = nullptr;

         JSValue stackVal = JS_GetPropertyStr(m_context, exception, "stack");
         if (!JS_IsUndefined(stackVal))
            stack = JS_ToCString(m_context, stackVal);

          LOGE("+----");
          LOGE("| Module exec error in %s:", fname);
          LOGE("| %s", msg ? msg : "unknown error");
          if (stack)
             LOGE("| %s", stack);
          LOGE("+----");

         if (msg) JS_FreeCString(m_context, msg);
         if (stack) JS_FreeCString(m_context, stack);
         JS_FreeValue(m_context, stackVal);
         JS_FreeValue(m_context, exception);
         JS_FreeValue(m_context, val);
         return false;
      }

      JS_FreeValue(m_context, val);
      return true;
   }

   bool Script::runString(std::string const& code, std::string const& filename)
   {
      started++;
      const char * fname = filename.empty() ? "<eval>" : filename.c_str();
      
      JSValue result = JS_Eval(m_context, code.c_str(), code.size(), fname, JS_EVAL_TYPE_GLOBAL);
      
      if (JS_IsException(result))
      {
         JSValue exception = JS_GetException(m_context);
         const char * msg = JS_ToCString(m_context, exception);
         const char * stack = nullptr;
         
         JSValue stackVal = JS_GetPropertyStr(m_context, exception, "stack");
         if (!JS_IsUndefined(stackVal))
               stack = JS_ToCString(m_context, stackVal);
         
         // 1. 输出到控制台（保持现有行为）
          LOGE("+----");
          LOGE("| Error in %s:", fname);
          LOGE("| %s", msg ? msg : "unknown error");
          if (stack)
                LOGE("| %s", stack);
          LOGE("+----");
         
         // 2. 同时报告给 JS 层的 console.error
         JSValue global = JS_GetGlobalObject(m_context);
         JSValue console = JS_GetPropertyStr(m_context, global, "console");
         if (!JS_IsUndefined(console) && !JS_IsNull(console)) {
               JSValue errorFn = JS_GetPropertyStr(m_context, console, "error");
               if (JS_IsFunction(m_context, errorFn)) {
                  // 构造错误消息
                  std::string errorMsg = std::string("Script error in ") + fname + ": " + (msg ? msg : "unknown");
                  JSValue args[1] = { JS_NewString(m_context, errorMsg.c_str()) };
                  JS_Call(m_context, errorFn, console, 1, args);
                  JS_FreeValue(m_context, args[0]);
               }
               JS_FreeValue(m_context, errorFn);
         }
         
         // 3. 触发 window.onerror 或 error 事件
         JSValue window = JS_GetPropertyStr(m_context, global, "window");
         if (!JS_IsUndefined(window) && !JS_IsNull(window)) {
               // 尝试调用 window.onerror
               JSValue onerror = JS_GetPropertyStr(m_context, window, "onerror");
               if (JS_IsFunction(m_context, onerror)) {
                  JSValue args[5];
                  args[0] = JS_NewString(m_context, msg ? msg : "unknown");
                  args[1] = JS_NewString(m_context, fname);
                  args[2] = JS_NewInt32(m_context, 0);  // lineno
                  args[3] = JS_NewInt32(m_context, 0);  // colno
                   args[4] = JS_DupValue(m_context, exception);
                  JS_Call(m_context, onerror, window, 5, args);
                  JS_FreeValue(m_context, args[0]);
                  JS_FreeValue(m_context, args[1]);
                  JS_FreeValue(m_context, args[2]);
                  JS_FreeValue(m_context, args[3]);
                  JS_FreeValue(m_context, args[4]);
               }
               JS_FreeValue(m_context, onerror);
               
               // 或者触发 error 事件
               JSValue dispatchEvent = JS_GetPropertyStr(m_context, window, "dispatchEvent");
               if (JS_IsFunction(m_context, dispatchEvent)) {
                  // 创建 ErrorEvent
                  JSValue errorEvent = JS_NewObject(m_context);
                  JS_SetPropertyStr(m_context, errorEvent, "type", JS_NewString(m_context, "error"));
                  JS_SetPropertyStr(m_context, errorEvent, "message", JS_NewString(m_context, msg ? msg : "unknown"));
                  JS_SetPropertyStr(m_context, errorEvent, "filename", JS_NewString(m_context, fname));
                   JS_SetPropertyStr(m_context, errorEvent, "error", JS_DupValue(m_context, exception));
                  
                  JSValue args[1] = { errorEvent };
                  JS_Call(m_context, dispatchEvent, window, 1, args);
                  JS_FreeValue(m_context, args[0]);
               }
               JS_FreeValue(m_context, dispatchEvent);
         }
         
         JS_FreeValue(m_context, window);
         JS_FreeValue(m_context, console);
         JS_FreeValue(m_context, global);
         
         // 清理
         if (msg) JS_FreeCString(m_context, msg);
         if (stack) JS_FreeCString(m_context, stack);
         JS_FreeValue(m_context, stackVal);
         JS_FreeValue(m_context, exception);
         JS_FreeValue(m_context, result);
         return false;
      }
      
      JS_FreeValue(m_context, result);
      return true;
   }

   static int m_nextTimerId;
   static int m_rafIndex;

   int Script::setInterval(JSValue function, int interval) {
     JSValue dup = JS_DupValue(m_context, function);
     double nowSec = m_absoluteTimer.seconds();
     int id = ++m_nextTimerId;
     TimerCallback cb(id, dup, interval, true, nowSec);
     m_timerCallbacks.insert(std::move(cb));
     return id;
   }

   int Script::setTimeout(JSValue function, int delay) {
     JSValue dup = JS_DupValue(m_context, function);
     double nowSec = m_absoluteTimer.seconds();
     int id = ++m_nextTimerId;
     TimerCallback cb(id, dup, delay, false, nowSec);
     m_timerCallbacks.insert(std::move(cb));
     return id;
   }

   void Script::clearInterval(int id) {
     auto it =
         std::find_if(m_timerCallbacks.begin(), m_timerCallbacks.end(),
                      [id](const TimerCallback &cb) { return cb.id == id; });
     if (it != m_timerCallbacks.end()) {
       JS_FreeValue(m_context, it->function);
       m_timerCallbacks.erase(it);
     }
   }
   WindowClass* Script::window(){
    return m_window;
   }
   void Script::clearTimeout(int id) { clearInterval(id); }
   int Script::requestAnimationFrame(JSValue function) {
     JSValue dup = JS_DupValue(m_context, function);
     m_rAFCallbacks.insert(std::make_pair(++m_rafIndex, dup));
     return m_rafIndex;
   }

   void Script::cancelAnimationFrame(int id) {
     auto it = m_rAFCallbacks.find(id);
     if (it != m_rAFCallbacks.end()) {
       JS_FreeValue(m_context, it->second);
       m_rAFCallbacks.erase(it);
     }
   }
   void Script::runGc(std::string msg){
      LOGI("[script] run gc: at code position mark: %s",msg.c_str());
      JS_RunGC(m_runtime);
   }
}