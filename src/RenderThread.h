#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <vector>

#include "util/Thread.h"
#include "util/Mutex.h"
#include "util/ScopedLock.h"
#include "util/CondVar.h"
#include "RenderProtocol.h"

struct SDL_Window;
typedef void* SDL_GLContext;

#ifdef __ANDROID__
#include <EGL/egl.h>
#include <android/native_window.h>
#endif

namespace canvas {

class CanvasRenderingContext2d;

class RenderThread : public Thread {
public:
    RenderThread();
    explicit RenderThread(SDL_Window* window);
#ifdef __ANDROID__
    RenderThread(EGLDisplay display, EGLContext sharedContext, EGLConfig config,
                 int width, int height, ANativeWindow* nativeWindow = nullptr);
#endif
    ~RenderThread() override;

    static RenderThread* instance() { return s_instance; }

    void init();
    void init(SDL_Window* window, SDL_GLContext sharedContext)
    {
        m_window = window;
        m_glContext = sharedContext;
        init();
    }
    void setGLContext(SDL_GLContext context) { m_glContext = context; }

    void submitFrame(std::vector<CompositeCommand> commands, uint64_t frameId);
    void waitForFrame(uint64_t frameId);
    void requestStop();
    bool isFrameComplete(uint64_t frameId) const;

    void sendGPUCommand(GPUCommand cmd);

#ifdef __ANDROID__
    void setSurface(ANativeWindow* window, int width, int height);
    void destroySurface();
#else
    void destroySurface() {}
#endif

private:
    void run() override;
    void processGPUCommands();
    void processSingleGPUCommand(GPUCommand& cmd);

    void createCompositedFBO(int width, int height);
    void destroyCompositedFBO();

    static RenderThread* s_instance;

    int m_compositedFbo = 0;
    int m_compositedTex = 0;
    int m_compositedRb = 0;
    int m_compositedWidth = 0;
    int m_compositedHeight = 0;

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_glContext = nullptr;

#ifdef __ANDROID__
    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    EGLContext m_eglContext = EGL_NO_CONTEXT;
    EGLContext m_sharedEglContext = EGL_NO_CONTEXT;
    EGLConfig m_eglConfig = nullptr;
    ANativeWindow* m_nativeWindow = nullptr;
    ANativeWindow* m_pendingWindow = nullptr;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;
    int m_surfaceWidth = 0;
    int m_surfaceHeight = 0;
#endif
    bool m_androidMode = false;

    struct FrameJob {
        std::vector<CompositeCommand> commands;
        uint64_t frameId = 0;
    };
    std::vector<FrameJob> m_jobQueue;
    mutable Mutex m_queueMutex;
    std::vector<GPUCommand> m_gpuCommands;

    std::atomic<uint64_t> m_lastCompletedFrame{0};
    std::atomic<uint64_t> m_lastSubmittedFrame{0};
    std::atomic<bool> m_running{true};
    bool m_pendingWakeup = false;

    CondVar m_workCond;
    Mutex m_completeMutex;
    CondVar m_completeCond;

    // GL fence per completed frame; main thread waits before reusing back buffer.
    std::map<uint64_t, void*> m_frameFences;

    struct CanvasResources {
        void* nvgCtx = nullptr;
        int fbo = 0, tex = 0, rb = 0;
        int width = 0, height = 0;
    };
    std::map<CanvasRenderingContext2d*, CanvasResources> m_canvasResources;
    void ensureCanvasResources(CanvasRenderingContext2d* ctx, int w, int h);
    void destroyCanvasResources(CanvasRenderingContext2d* ctx);
};

} // namespace canvas
