#pragma once

#include "CanvasAppBase.h"
#include "RenderThread.h"

struct SDL_Window;
typedef void* SDL_GLContext;

#ifdef __ANDROID__
#include <EGL/egl.h>
#include <android/native_window.h>

namespace canvas {

class CanvasApp : public CanvasAppBase {
public:
    CanvasApp(int width, int height);
    ~CanvasApp() override;

    void makeCurrent() override;
    void setSize(int winWidth, int winHeight, int outerWidth, int outerHeight) override;

    void initEGL(int width, int height);
    void teardownEGL();
    void setSurface(ANativeWindow* window, int width, int height);
    void destroySurface();
    void renderFrame();
    void RenderFrame() { renderFrame(); }
    void resize(int width, int height);
    void Update() { TickScript(); }

    bool isEGLReady() const { return m_eglReady; }

private:
    int m_width = 0;
    int m_height = 0;
    bool m_eglReady = false;

    int m_surfaceWidth = 0;
    int m_surfaceHeight = 0;
    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    EGLConfig m_eglConfig = nullptr;
    EGLContext m_mainEglContext = EGL_NO_CONTEXT;
    EGLContext m_sharedEglContext = EGL_NO_CONTEXT;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
};

} // namespace canvas

#else

namespace canvas {

class CanvasApp : public CanvasAppBase {
public:
    CanvasApp(SDL_Window* window, SDL_GLContext primaryContext,
              int innerW, int innerH, int outerW, int outerH);
    ~CanvasApp() override;

    void setSize(int winWidth, int winHeight, int outerWidth, int outerHeight) override;
    void RenderFrame();

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_glContext = nullptr;
    RenderThread* m_renderThread = nullptr;
};

} // namespace canvas

#endif
