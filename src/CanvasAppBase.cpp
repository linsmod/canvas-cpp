#include "Bindings.h"
#include "Log.h"
#include "Orientation.h"
#include "PlatformFileSystem.h"
#include "PlatformScreen.h"
#ifdef __ANDROID__
#include <EGL/egl.h>
#include <android/native_window.h>
#include <GLES3/gl3.h>
#else
#include "SDL2/SDL.h"
#include "SDL_video.h"
#include <GL/glcorearb.h>
#endif
#include "Script.h"
#include <any>
#include <memory>
#include <string>
#include "CanvasApp.h"
#include "quickjs.h"
#include "quickjs-libc.h"
#include "RenderNode.h"
#include "Layout.h"
#include "Perf.h"
#include "util/Thread.h"
#include "EventDispatch.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <chrono>
#include "util/Thread.h"
#include <pthread.h>
#include <unistd.h>
#include <pthread.h>

namespace canvas {
    
    // ========== CanvasAppBase ==========
    
    CanvasAppBase::CanvasAppBase(int innerW, int innerH, int outerW, int outerH)
    :
    m_baseDir(""),
    jsOuterHeight(0),
    jsOuterWidth(0),
    windowHeight(0),
    windowWidth(0),
    jsInnerHeight(0),
    jsInnerWidth(0),
    m_fs(createPlatformFileSystem("")),
    m_screen(createPlatformScreen()),
    m_script(nullptr),
    m_runtime(JS_NewRuntime()),
    m_jsContext(JS_NewContext(m_runtime)),
    m_lastPageScale(1.0f),
    m_paused(false)
    {
        JS_SetGCThreshold(m_runtime, 16 * 1024 * 1024);
        JS_SetRuntimeOpaque(m_runtime, this);
        JS_SetModuleLoaderFunc2(m_runtime, NULL, js_module_loader,
                          js_module_check_attributes, NULL);
        m_script = new Script(m_jsContext);
        
        this->setSize(innerW, innerH, outerW, outerH);
        auto html = m_script->window()->document()->html();
        m_renderTree  = buildRenderTree(m_jsContext, html);
        html->setDirtyListenr(m_renderTree.get());
    }

    void CanvasAppBase::setPageScale(float scale) {
        if (pageScale != scale) {
            pageScale = scale;
            setSize(windowWidth, windowHeight, jsOuterWidth, jsOuterHeight);
        }
    }
    
     void CanvasAppBase::setSize(int winWidth, int winHeight, int outerWidth, int outerHeight){
        LOGI("[setSize] win=%dx%d outer=%dx%d (old: win=%dx%d canvas=%dx%d jsInner=%dx%d jsOuter=%dx%d)",
             winWidth, winHeight, outerWidth, outerHeight,
             windowWidth, windowHeight, canvasWidth, canvasHeight,
             jsInnerWidth, jsInnerHeight, jsOuterWidth, jsOuterHeight);
        if(winWidth>0 && winHeight>0){
            bool sizeChanged = (windowWidth != winWidth || windowHeight != winHeight);
            bool scaleChanged = (m_lastPageScale != pageScale);
            
            if(sizeChanged || scaleChanged){
                windowWidth = winWidth;
                windowHeight = winHeight;
                m_sizeDirty = true;
                
                m_lastPageScale = pageScale;
                
                float totalScale = pageScale * dpiScale;
                this->jsInnerWidth  = static_cast<int>(winWidth  / totalScale);
                this->jsInnerHeight = static_cast<int>(winHeight / totalScale);
                this->jsOuterWidth = static_cast<int>(winWidth  / dpiScale);
                this->jsOuterHeight = static_cast<int>(winHeight / dpiScale);
                this->canvasWidth  = winWidth;
                this->canvasHeight = winHeight;
            }
        }
    }
    
    void CanvasAppBase::buildRenderTreeAndPaint(uintptr_t targetFbo, int w, int h) {
        auto t0 = std::chrono::steady_clock::now();
        m_script->executeRAFCallbacks();
        auto t1 = std::chrono::steady_clock::now();
        bool layoutRebuilt = false;

        // Only rebuild layout when layout is dirty or size changed
        if (m_sizeDirty || (m_renderTree && m_renderTree->hasDirty(LAYOUT)) || !m_renderTree) {
            m_sizeDirty = false;
            auto window = m_script->window();
            if(window && window->document()){
                HtmlClass* html = window->document()->html();
                if (html) {
                    m_renderTree = buildRenderTree(m_jsContext, html);
                    html->setDirtyListenr(m_renderTree.get());
                    if (m_renderTree) {
                        layoutTree(m_renderTree.get(), (float)jsInnerWidth, (float)jsInnerHeight);
                        m_renderTree.get()->clearDirty(LAYOUT);
                    }
                    layoutRebuilt = true;
                }
            }
        }
        auto t1b = std::chrono::steady_clock::now();

        // Always walk tree for CompositeCommand generation (render thread needs it every frame)
        bool alwaysPaint = (m_renderTree != nullptr);
        bool needsRepaint = alwaysPaint || layoutRebuilt || m_renderTree->hasDirty(CONTENT);

        auto t2 = std::chrono::steady_clock::now();
        if (needsRepaint && m_renderTree) {
            m_renderTree->paint(reinterpret_cast<void*>(targetFbo), w, h, m_jsContext);
            // Clear both dirtied to prevent hasDirty(LAYOUT) from iterating stale bubbles
            m_renderTree->clearDirty(CONTENT);
            m_renderTree->clearDirty(LAYOUT);
        }
        auto t3 = std::chrono::steady_clock::now();
        auto rAfUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        auto layoutUs = std::chrono::duration_cast<std::chrono::microseconds>(t1b - t1).count();
        auto paintUs = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
        if (rAfUs > 50000)
            fprintf(stderr, "[CANVAS] rAf=%lldus layout=%lldus paint=%lldus\n",
                    (long long)rAfUs, (long long)layoutUs, (long long)paintUs);

        if (!m_WindowLoad_dispached && m_renderTree && m_scriptStarted) {
            m_WindowLoad_dispached = true;
            dispatchWindowLoadEvent(m_jsContext);
        }
    }

    Script* CanvasAppBase::getScript(){
        return this->m_script;
    }

    bool CanvasAppBase::isDirty() const
    {
        return m_renderTree==nullptr||m_renderTree->hasDirty(CONTENT)||m_renderTree->hasDirty(LAYOUT);
    }
   
    void CanvasAppBase::TickScript() {
       if (!m_DOMContentLoaded_dispached && m_scriptStarted) {
          m_DOMContentLoaded_dispached = true;
          dispatchDOMContentLoaded(m_jsContext);
       }
       if(m_script->tickScript(10)){
       }
    }

    void CanvasAppBase::SetBaseDir(std::string dir){
        m_baseDir = dir;
        m_fs = createPlatformFileSystem(dir);
    }

     void CanvasAppBase::runScript(std::string const &fileOrCode, bool isFile) {
        if(this->m_scriptStarted){
            LOGW("Main script already evaluted");
            return;
        }
        if (isFile)
        {
            std::string entrySource = m_fs ? m_fs->readFile(fileOrCode) : std::string();
            if (entrySource.empty()) {
                LOGE("Failed to read entry script source: %s", fileOrCode.c_str());
                // exit(__LINE__);
                return;
            }
            if (!m_script->runString(entrySource, fileOrCode)) {
                LOGE("Failed to load entry script source: %s", fileOrCode.c_str());
                // exit(__LINE__);
                return;
            }
        }
        else{
            if (!m_script->runString(fileOrCode)) {
                LOGE("Failed to load entry script source: %s", fileOrCode.c_str());
                // exit(__LINE__);
                return;
            }
        }
        m_scriptStarted = true;
    }

    void CanvasAppBase::runModule(std::string const &entryFile) {
        if(this->m_scriptStarted){
            LOGW("Main script already evaluted");
            return;
        }
        std::string entrySource = m_fs ? m_fs->readFile(entryFile) : std::string();
        if (entrySource.empty()) {
            LOGE("Failed to read entry module: %s", entryFile.c_str());
            // exit(__LINE__);
            return;
        }

        // Build full module path so QuickJS can resolve relative imports (e.g. "./Engine.js")
        // against the script's directory rather than the CWD.
        std::string modulePath;
        if (!m_baseDir.empty() && entryFile.find('/') == std::string::npos) {
            modulePath = m_baseDir + "/" + entryFile;
        } else {
            modulePath = entryFile;
        }

        if (!m_script->runModule(entrySource, modulePath)) {
            LOGE("Failed to load entry module: %s", entryFile.c_str());
            // exit(__LINE__);
            return;
        }

        LOGI("[Painter] ENTRY MODULE EXECUTION COMPLETE");
        m_scriptStarted = true;
    }

    JSContext* CanvasAppBase::getJsContext(){
        return this->m_jsContext;
    }

    void CanvasAppBase::mapNodeSelf(NodeClass* node, JSValue value){
        ScopedLock lock(m_nodemapMutex);
        if (node) {
            m_nodemapself[node] = value;
            JS_RunGC(m_runtime);
        }
    }

    JSValue CanvasAppBase::getNodeJSValue(NodeClass* node) const {
        ScopedLock lock(const_cast<CanvasAppBase*>(this)->m_nodemapMutex);
        auto it = m_nodemapself.find(node);
        if (it != m_nodemapself.end()) {
            if(JS_IsUndefined(it->second)){
                LOGI("[DEBUG] getNodeJSValue: Got Undefined JSValue of %p %s in nodemap (size=%zu)", node,node->getClassName().c_str(), m_nodemapself.size());
            }
            return JS_DupValue(m_jsContext, it->second);
        }
        LOGI("[DEBUG] getNodeJSValue: MISSING JSValue of %p %s in nodemap (size=%zu)", node,node->getClassName().c_str(), m_nodemapself.size());
        for (auto& kv : m_nodemapself) {
            LOGI("[DEBUG]   nodemap key: %p", kv.first);
        }
        return JS_UNDEFINED;
    }

    void CanvasAppBase::unmapNodeSelf(NodeClass* node) {
        ScopedLock lock(m_nodemapMutex);
        if (node) {
            // LOGI("forgetNode : %s %p",node->getClassName().c_str(),node);
            m_nodemapself.erase(node);
        }
        else{
            LOGW("forgetNode of null?");
        }
    }

    void CanvasAppBase::dispatchVisibilityChanged(bool hidden) {
      canvas::dispatchVisibilityChanged(this->m_jsContext, hidden);
    }

    void CanvasAppBase::dispatchOrientationChanged() {
      canvas::dispatchOrientationChanged(this->m_jsContext);
    }

    void CanvasAppBase::dispatchWindowFocusEvent(bool focused) {
      canvas::dispatchWindowFocusEvent(this->m_jsContext, focused);
    }

    void CanvasAppBase::dispatchMouseEvent(const char *type, int x, int y,
                                       int button, int buttons) {
      canvas::dispatchMouseEvent(this->m_jsContext, type, x, y, button,
                                 buttons);
    }

    void CanvasAppBase::dispatchWheelEvent(const char *type, int x, int y,
                                       int deltaY, bool ctrlKey, bool shiftKey,
                                       bool altKey, bool metaKey) {
      canvas::dispatchWheelEvent(this->m_jsContext, type, x, y, deltaY, ctrlKey,
                                 shiftKey, altKey, metaKey);
    }

    void CanvasAppBase::dispatchKeyEvent(const char *type, const char *key,
                                     bool ctrlKey, bool shiftKey, bool altKey,
                                     bool metaKey) {
      canvas::dispatchKeyEvent(this->m_jsContext, type, key, ctrlKey, shiftKey,
                               altKey, metaKey);
    }

    int CanvasAppBase::getScaledWidth() {
        return static_cast<int>(windowWidth / (pageScale * dpiScale));
    }

    int CanvasAppBase::getScaledHeight() {
        return static_cast<int>(windowHeight / (pageScale * dpiScale));
    }

    CanvasAppBase::~CanvasAppBase() {
        LOGI("");
        LOGI("Exit: clean res...");
        if(m_script){
            delete m_script;
        }

        JS_FreeContext(m_jsContext);
        JS_FreeRuntime(m_runtime);
    }

    JSValue CanvasAppBase::getImage(std::string const &name) {
      std::map<std::string, ImageData *>::iterator result = m_images.find(name);
      if (result == m_images.end())
        return JS_UNDEFINED;

      JSValue obj = JS_NewObjectClass(m_jsContext, ImageClass::getClassID());
      JS_SetOpaque(obj, result->second);
      return obj;
    }

#ifdef __ANDROID__
    // ========== CanvasApp (Android EGL + RenderThread) ==========

    CanvasApp::CanvasApp(int w, int h)
    : CanvasAppBase(w, h, w, h),
      m_width(w),
      m_height(h)
    {}

    CanvasApp::~CanvasApp() {
        teardownEGL();
    }

    void CanvasApp::initEGL(int width, int height) {
        m_width = width;
        m_height = height;

        m_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (m_eglDisplay == EGL_NO_DISPLAY) {
            LOGE("[CanvasApp/EGL] eglGetDisplay FAILED thread=%p", (void*)pthread_self());
            return;
        }
        LOGI("[CanvasApp/EGL] eglGetDisplay OK display=%p thread=%p", (void*)m_eglDisplay, (void*)pthread_self());

        EGLint major, minor;
        if (!eglInitialize(m_eglDisplay, &major, &minor)) {
            LOGE("[CanvasApp/EGL] eglInitialize FAILED thread=%p", (void*)pthread_self());
            return;
        }
        LOGI("[CanvasApp/EGL] eglInitialize OK EGL %d.%d thread=%p", major, minor, (void*)pthread_self());

        const EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
            EGL_NONE
        };

        EGLint numConfigs;
        if (!eglChooseConfig(m_eglDisplay, attribs, &m_eglConfig, 1, &numConfigs) || numConfigs == 0) {
            LOGE("[CanvasApp/EGL] eglChooseConfig FAILED thread=%p", (void*)pthread_self());
            return;
        }
        LOGI("[CanvasApp/EGL] eglChooseConfig OK config=%p thread=%p", (void*)m_eglConfig, (void*)pthread_self());

        const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };

        m_mainEglContext = eglCreateContext(m_eglDisplay, m_eglConfig, EGL_NO_CONTEXT, ctxAttribs);
        if (m_mainEglContext == EGL_NO_CONTEXT) {
            LOGE("[CanvasApp/EGL] eglCreateContext(main) FAILED err=0x%x thread=%p", eglGetError(), (void*)pthread_self());
            return;
        }
        LOGI("[CanvasApp/EGL] eglCreateContext(main) OK ctx=%p thread=%p", (void*)m_mainEglContext, (void*)pthread_self());

        m_sharedEglContext = eglCreateContext(m_eglDisplay, m_eglConfig, m_mainEglContext, ctxAttribs);
        if (m_sharedEglContext == EGL_NO_CONTEXT) {
            LOGE("[CanvasApp/EGL] eglCreateContext(shared) FAILED err=0x%x thread=%p", eglGetError(), (void*)pthread_self());
            eglDestroyContext(m_eglDisplay, m_mainEglContext);
            m_mainEglContext = EGL_NO_CONTEXT;
            return;
        }
        LOGI("[CanvasApp/EGL] eglCreateContext(shared) OK ctx=%p thread=%p", (void*)m_sharedEglContext, (void*)pthread_self());

        LOGI("[CanvasApp/EGL] initEGL done (no surface) thread=%p", (void*)pthread_self());

        m_renderThread = std::make_unique<RenderThread>(
            m_eglDisplay, m_sharedEglContext, m_eglConfig, width, height);
        m_renderThread->init();

        m_eglReady = true;
    }

    void CanvasApp::teardownEGL() {
        if (!m_eglReady) return;

        if (m_renderThread) {
            m_renderThread->requestStop();
            m_renderThread.reset();
        }

        if (m_eglDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

            if (m_eglSurface != EGL_NO_SURFACE) {
                eglDestroySurface(m_eglDisplay, m_eglSurface);
                m_eglSurface = EGL_NO_SURFACE;
            }
            if (m_sharedEglContext != EGL_NO_CONTEXT) {
                eglDestroyContext(m_eglDisplay, m_sharedEglContext);
                m_sharedEglContext = EGL_NO_CONTEXT;
            }
            if (m_mainEglContext != EGL_NO_CONTEXT) {
                eglDestroyContext(m_eglDisplay, m_mainEglContext);
                m_mainEglContext = EGL_NO_CONTEXT;
            }
            eglTerminate(m_eglDisplay);
            m_eglDisplay = EGL_NO_DISPLAY;
        }

        m_eglConfig = nullptr;
        m_eglReady = false;
        LOGI("[CanvasApp] EGL torn down");
    }

    void CanvasApp::setSurface(ANativeWindow* window, int width, int height) {
        if (!m_eglReady) {
            initEGL(width, height);
            return;
        }

        if (m_renderThread) {
            m_renderThread->destroySurface();
        }

        if (m_eglSurface != EGL_NO_SURFACE) {
            eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroySurface(m_eglDisplay, m_eglSurface);
            m_eglSurface = EGL_NO_SURFACE;
        }

        m_width = width;
        m_height = height;

        LOGI("[CanvasApp/EGL] setSurface relay to RenderThread %dx%d window=%p thread=%p",
             width, height, (void*)window, (void*)pthread_self());

        if (m_renderThread) {
            m_renderThread->setSurface(window, width, height);
        }
    }

    void CanvasApp::destroySurface() {
        if (m_renderThread) {
            m_renderThread->destroySurface();
        }

        if (m_eglSurface != EGL_NO_SURFACE) {
            eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroySurface(m_eglDisplay, m_eglSurface);
            m_eglSurface = EGL_NO_SURFACE;
        }
        LOGI("[CanvasApp] Surface destroyed");
    }

    void CanvasApp::renderFrame() {
        if (!m_eglReady || !m_renderThread) return;

        buildRenderTreeAndPaint(0, m_width, m_height);

        if (canvas::g_renderCommands.empty()) return;

        uint64_t frameId = ++m_frameId;
        m_renderThread->submitFrame(canvas::g_renderCommands, frameId);
        m_renderThread->waitForFrame(frameId);
    }

    void CanvasApp::resize(int w, int h) {
        m_width = w;
        m_height = h;
        CanvasAppBase::setSize(w, h, w, h);
    }
#endif

#ifndef __ANDROID__
    // ========== CanvasApp (Desktop SDL) ==========

    CanvasApp::CanvasApp(SDL_Window* window, SDL_GLContext primaryCtx, int innerW, int innerH, int outerW, int outerH)
    : CanvasAppBase(innerW, innerH, outerW, outerH),
      m_window(window),
      m_glContext(primaryCtx),
      m_renderThread(nullptr)
    {
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
        SDL_GLContext sharedCtx = SDL_GL_CreateContext(window);
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);

        if (sharedCtx) {
            SDL_GL_MakeCurrent(window, m_glContext);
            m_renderThread = new RenderThread();
            m_renderThread->init(window,sharedCtx);
            LOGI("[CanvasApp] Render thread created with shared GL context");
        } else {
            LOGE("[CanvasApp] Failed to create shared GL context: %s", SDL_GetError());
        }
    }

    CanvasApp::~CanvasApp() {
        if (m_renderThread) {
            m_renderThread->requestStop();
            delete m_renderThread;
            m_renderThread = nullptr;
        }
    }

    void CanvasApp::setSize(int winWidth, int winHeight, int outerWidth, int outerHeight) {
        CanvasAppBase::setSize(winWidth, winHeight, outerWidth, outerHeight);
    }

     void CanvasApp::RenderFrame() {
          Perf::reset();
          g_renderCommands.clear();
          buildRenderTreeAndPaint((uintptr_t)0, canvasWidth, canvasHeight);

          if (m_renderThread && !g_renderCommands.empty()) {
              uint64_t frameId = ++m_frameId;
              m_renderThread->submitFrame(g_renderCommands, frameId);
              m_renderThread->waitForFrame(frameId);
          }

          Perf::dump();
          if (++m_gcCounter % 60 == 0) JS_RunGC(m_runtime);
          SDL_GL_SwapWindow(m_window);
     }
#endif
}