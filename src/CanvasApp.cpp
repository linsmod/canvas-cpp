#ifdef __ANDROID__

#include <EGL/egl.h>
#include <EGL/eglext.h>

// 错误检查宏
#define EGL_CHECK(msg) do { \
    EGLint err = eglGetError(); \
    if (err != EGL_SUCCESS) { \
        LOGE("%s failed: 0x%x", msg, err); \
        return false; \
    } \
} while(0)

bool CanvasApp::initEGL(int width, int height) {
    // 1. 获取 EGLDisplay
    m_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        EGL_CHECK("eglGetDisplay");
        return false;
    }

    // 2. 初始化 EGL
    EGLint major, minor;
    if (!eglInitialize(m_eglDisplay, &major, &minor)) {
        EGL_CHECK("eglInitialize");
        return false;
    }
    LOGI("EGL initialized: version %d.%d", major, minor);

    // 3. 选择配置（RGBA8888 + 深度缓冲）
    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    EGLint numConfigs;
    if (!eglChooseConfig(m_eglDisplay, configAttribs, &m_eglConfig, 1, &numConfigs) || numConfigs == 0) {
        EGL_CHECK("eglChooseConfig");
        return false;
    }

    // 4. 创建 PBuffer 表面（离屏渲染）
    const EGLint pbufferAttribs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_NONE
    };
    m_eglSurface = eglCreatePbufferSurface(m_eglDisplay, m_eglConfig, pbufferAttribs);
    if (m_eglSurface == EGL_NO_SURFACE) {
        EGL_CHECK("eglCreatePbufferSurface");
        return false;
    }
    m_surfaceWidth = width;
    m_surfaceHeight = height;

    // 5. 创建主线程 EGLContext（OpenGL ES 3.0）
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    m_eglContext = eglCreateContext(m_eglDisplay, m_eglConfig, EGL_NO_CONTEXT, contextAttribs);
    if (m_eglContext == EGL_NO_CONTEXT) {
        EGL_CHECK("eglCreateContext (main)");
        return false;
    }

    // 6. 创建渲染线程共享 context（与主线程共享纹理等资源）
    m_eglRenderContext = eglCreateContext(m_eglDisplay, m_eglConfig, m_eglContext, contextAttribs);
    if (m_eglRenderContext == EGL_NO_CONTEXT) {
        EGL_CHECK("eglCreateContext (render)");
        return false;
    }

    // 7. 主线程暂不 make current，等首次使用时再做
    LOGI("EGL initialized: pbuffer %dx%d, main ctx=%p, render ctx=%p",
         width, height, m_eglContext, m_eglRenderContext);
    return true;
}

void CanvasApp::destroyEGL() {
    if (m_eglDisplay == EGL_NO_DISPLAY) return;

    // 解绑当前 context
    eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (m_eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(m_eglDisplay, m_eglSurface);
        m_eglSurface = EGL_NO_SURFACE;
    }
    if (m_eglContext != EGL_NO_CONTEXT) {
        eglDestroyContext(m_eglDisplay, m_eglContext);
        m_eglContext = EGL_NO_CONTEXT;
    }
    if (m_eglRenderContext != EGL_NO_CONTEXT) {
        eglDestroyContext(m_eglDisplay, m_eglRenderContext);
        m_eglRenderContext = EGL_NO_CONTEXT;
    }
    eglTerminate(m_eglDisplay);
    m_eglDisplay = EGL_NO_DISPLAY;
}

void CanvasApp::recreateSurface(int width, int height) {
    if (m_eglDisplay == EGL_NO_DISPLAY) return;
    if (m_eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(m_eglDisplay, m_eglSurface);
        m_eglSurface = EGL_NO_SURFACE;
    }
    const EGLint pbufferAttribs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_NONE
    };
    m_eglSurface = eglCreatePbufferSurface(m_eglDisplay, m_eglConfig, pbufferAttribs);
    if (m_eglSurface == EGL_NO_SURFACE) {
        EGL_CHECK("eglCreatePbufferSurface (recreate)");
    } else {
        m_surfaceWidth = width;
        m_surfaceHeight = height;
        LOGI("Pbuffer surface recreated: %dx%d", width, height);
    }
}

void CanvasApp::makeCurrent() {
    // 根据调用线程选择 context：渲染线程使用 m_eglRenderContext，其他线程使用 m_eglContext
    EGLContext currentContext = (pthread_equal(pthread_self(), m_renderThread->getThreadId()) ?
                                 m_eglRenderContext : m_eglContext);
    if (currentContext == EGL_NO_CONTEXT) {
        LOGE("makeCurrent: no valid context for thread %p", (void*)pthread_self());
        return;
    }
    if (m_eglSurface == EGL_NO_SURFACE) {
        LOGE("makeCurrent: no surface");
        return;
    }
    if (!eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, currentContext)) {
        EGLint err = eglGetError();
        LOGE("eglMakeCurrent failed: 0x%x", err);
    }
}

CanvasApp::CanvasApp(int w, int h)
    : CanvasAppBase(w, h, w, h),
      m_frameId(0),
      m_gcCounter(0)
{
    // 初始化 EGL
    if (!initEGL(w, h)) {
        LOGE("Failed to initialize EGL");
        // 可考虑退出或 fallback
    }

    // 创建渲染线程，传递 makeCurrent 回调
    auto makeCurrentCallback = [this]() {
        this->makeCurrent();
    };
    m_renderThread = new RenderThread(makeCurrentCallback);
    m_renderThread->init();
}

CanvasApp::~CanvasApp() {
    destroyEGL();
}

void CanvasApp::setSize(int winWidth, int winHeight, int outerWidth, int outerHeight) {
    CanvasAppBase::setSize(winWidth, winHeight, outerWidth, outerHeight);
    // 如果窗口尺寸改变，需要重新创建 pbuffer surface
    if (m_eglDisplay != EGL_NO_DISPLAY && (winWidth != m_surfaceWidth || winHeight != m_surfaceHeight)) {
        recreateSurface(winWidth, winHeight);
    }
}

#endif // __ANDROID__