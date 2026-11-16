#include "RenderThread.h"
#include "Context.h"
#include "DisplayList.h"
#include "Log.h"
#include "Perf.h"
#ifndef __ANDROID__
#include <SDL2/SDL.h>
#endif
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#endif
#include <EGL/egl.h>
#include "NanoVG_GL.h"
#include <algorithm>
#ifdef __ANDROID__
#include <pthread.h>
#endif
#include <unistd.h>

namespace canvas {

RenderThread* RenderThread::s_instance = nullptr;

RenderThread::RenderThread()
    : Thread(false)
    , m_androidMode(false)
{
    s_instance = this;
}

RenderThread::RenderThread(SDL_Window* window)
    : Thread(false)
    , m_window(window)
    , m_glContext(nullptr)
    , m_androidMode(false)
{
    s_instance = this;
}

#ifdef __ANDROID__
RenderThread::RenderThread(EGLDisplay display, EGLContext sharedContext,
                           EGLConfig config, int width, int height)
    : Thread(false)
    , m_window(nullptr)
    , m_glContext(nullptr)
    , m_eglDisplay(display)
    , m_sharedEglContext(sharedContext)
    , m_eglConfig(config)
    , m_surfaceWidth(width)
    , m_surfaceHeight(height)
    , m_androidMode(true)
{
    s_instance = this;
}
#endif

void RenderThread::init() {
    start();
}

RenderThread::~RenderThread() {
    s_instance = nullptr;
    requestStop();
}

void RenderThread::sendGPUCommand(GPUCommand cmd) {
    ScopedLock lock(m_queueMutex);
    m_gpuCommands.push_back(std::move(cmd));
    m_workCond.signal();
}

void RenderThread::submitFrame(std::vector<CompositeCommand> commands, uint64_t frameId) {
    ScopedLock lock(m_queueMutex);
    m_jobQueue.push_back({std::move(commands), frameId});
    m_lastSubmittedFrame.store(frameId);
    m_pendingWakeup = true;
    m_workCond.signal();
}

void RenderThread::waitForFrame(uint64_t frameId) {
    GLsync fence = nullptr;
    {
        ScopedLock lock(m_completeMutex);
        while (m_lastCompletedFrame.load() < frameId) {
            m_completeCond.wait(m_completeMutex);
        }

        auto it = m_frameFences.find(frameId);
        if (it == m_frameFences.end()) {
            it = m_frameFences.find(m_lastCompletedFrame.load());
        }
        if (it != m_frameFences.end()) {
            fence = reinterpret_cast<GLsync>(it->second);
            m_frameFences.erase(it);
        }

        for (auto stale = m_frameFences.begin(); stale != m_frameFences.end();) {
            if (stale->first < frameId) {
                if (stale->second) glDeleteSync(reinterpret_cast<GLsync>(stale->second));
                stale = m_frameFences.erase(stale);
            } else {
                ++stale;
            }
        }
    }

    if (!fence) return;
    GLenum status = glClientWaitSync(fence, 0, 1000000000ULL);
    glDeleteSync(fence);
    if (status == GL_TIMEOUT_EXPIRED) {
        LOGW("[RenderThread] glClientWaitSync timed out for frame %llu",
             static_cast<unsigned long long>(frameId));
    }
}

bool RenderThread::isFrameComplete(uint64_t frameId) const {
    return m_lastCompletedFrame.load() >= frameId;
}

void RenderThread::requestStop() {
    m_running.store(false);
    m_pendingWakeup = true;
    m_workCond.signal();
    m_completeCond.broadcast();
}

#ifdef __ANDROID__
void RenderThread::setSurface(ANativeWindow* window, int width, int height) {
    ScopedLock lock(m_queueMutex);
    if (m_pendingWindow) {
        ANativeWindow_release(m_pendingWindow);
    }
    if (window) {
        ANativeWindow_acquire(window);
    }
    m_pendingWindow = window;
    m_pendingWidth = width;
    m_pendingHeight = height;
    m_workCond.signal();
}

void RenderThread::destroySurface() {
    ScopedLock lock(m_queueMutex);
    if (m_eglSurface != EGL_NO_SURFACE) {
        eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(m_eglDisplay, m_eglSurface);
        m_eglSurface = EGL_NO_SURFACE;
    }
    if (m_pendingWindow) {
        ANativeWindow_release(m_pendingWindow);
        m_pendingWindow = nullptr;
    }
    m_surfaceWidth = 0;
    m_surfaceHeight = 0;
}

void RenderThread::createCompositedFBO(int w, int h) {
    destroyCompositedFBO();

    glGenTextures(1, (GLuint*)&m_compositedTex);
    glBindTexture(GL_TEXTURE_2D, (GLuint)m_compositedTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, (GLuint*)&m_compositedRb);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)m_compositedRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, (GLuint*)&m_compositedFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_compositedFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           (GLuint)m_compositedTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, (GLuint)m_compositedRb);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("[RenderThread] Composited FBO incomplete: 0x%x %dx%d", status, w, h);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    LOGI("[RenderThread] Composited FBO created %dx%d", w, h);
}

void RenderThread::destroyCompositedFBO() {
    if (m_compositedFbo) {
        glDeleteFramebuffers(1, (GLuint*)&m_compositedFbo);
        m_compositedFbo = 0;
    }
    if (m_compositedTex) {
        glDeleteTextures(1, (GLuint*)&m_compositedTex);
        m_compositedTex = 0;
    }
    if (m_compositedRb) {
        glDeleteRenderbuffers(1, (GLuint*)&m_compositedRb);
        m_compositedRb = 0;
    }
}
#endif

// ── GPU command processing ─────────────────────────────────────────────

void RenderThread::processSingleGPUCommand(GPUCommand& cmd) {
    switch (cmd.type) {
        case GPUCommandType::CreateFBO: {
            GLuint fbo, tex, rb;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cmd.width, cmd.height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, tex, 0);

            glGenRenderbuffers(1, &rb);
            glBindRenderbuffer(GL_RENDERBUFFER, rb);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                                  cmd.width, cmd.height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, rb);

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            bool ok = (status == GL_FRAMEBUFFER_COMPLETE);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);

            cmd.createPromise.set_value({(int)fbo, (int)tex, (int)rb, ok});
            break;
        }
        case GPUCommandType::DeleteFBO: {
            GLuint fbo = (GLuint)cmd.fboId;
            GLuint tex = (GLuint)cmd.texId;
            GLuint rb = (GLuint)cmd.rbId;
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            glDeleteRenderbuffers(1, &rb);
            cmd.simplePromise.set_value(true);
            break;
        }
        case GPUCommandType::ReadPixels: {
            std::vector<uint8_t> pixels(cmd.readW * cmd.readH * 4);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)cmd.readFbo);
            glReadPixels(cmd.readX, cmd.readY, cmd.readW, cmd.readH,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            cmd.readPromise.set_value(std::move(pixels));
            break;
        }
        case GPUCommandType::CleanupCanvas: {
            destroyCanvasResources(cmd.cleanupCtx);
            cmd.simplePromise.set_value(true);
            break;
        }
    }
}

void RenderThread::processGPUCommands() {
    std::vector<GPUCommand> cmds;
    {
        ScopedLock lock(m_queueMutex);
        if (!m_gpuCommands.empty()) {
            cmds.swap(m_gpuCommands);
        }
    }
    for (auto& cmd : cmds) {
        processSingleGPUCommand(cmd);
    }
}

// ── Canvas resource management ─────────────────────────────────────────

void RenderThread::ensureCanvasResources(CanvasRenderingContext2d* ctx, int w, int h) {
    auto it = m_canvasResources.find(ctx);
    if (it != m_canvasResources.end()) {
        if (it->second.width == w && it->second.height == h) {
            return;
        }
        destroyCanvasResources(ctx);
        it = m_canvasResources.end();
    }

    CanvasResources res;
    res.nvgCtx = nullptr;
    res.fbo = 0;
    res.tex = 0;
    res.rb = 0;
    res.width = w;
    res.height = h;

    glGenTextures(1, (GLuint*)&res.tex);
    glBindTexture(GL_TEXTURE_2D, (GLuint)res.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, (GLuint*)&res.rb);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)res.rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, (GLuint*)&res.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)res.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           (GLuint)res.tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, (GLuint)res.rb);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("[RenderThread] Canvas FBO incomplete: 0x%x", status);
    }

#ifdef __ANDROID__
    NVGcontext* nvg = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#else
    NVGcontext* nvg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#endif
    if (!nvg) {
        LOGE("[RenderThread] Failed to create NVG context for canvas");
    }
    res.nvgCtx = (void*)nvg;

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_canvasResources[ctx] = res;
}

void RenderThread::destroyCanvasResources(CanvasRenderingContext2d* ctx) {
    auto it = m_canvasResources.find(ctx);
    if (it == m_canvasResources.end()) return;

    if (it->second.nvgCtx) {
#ifdef __ANDROID__
        nvgDeleteGLES3((NVGcontext*)it->second.nvgCtx);
#else
        nvgDeleteGL3((NVGcontext*)it->second.nvgCtx);
#endif
    }
    if (it->second.fbo) {
        glDeleteFramebuffers(1, (GLuint*)&it->second.fbo);
    }
    if (it->second.tex) {
        glDeleteTextures(1, (GLuint*)&it->second.tex);
    }
    if (it->second.rb) {
        glDeleteRenderbuffers(1, (GLuint*)&it->second.rb);
    }
    m_canvasResources.erase(it);
}

// ── Render thread main loop ────────────────────────────────────────────

void RenderThread::run() {
#ifndef __ANDROID__
    // Desktop: wait for SDL shared context
    while (m_glContext == nullptr && m_running.load()) {
        usleep(1000);
    }
    if (!m_running.load()) return;

    if (SDL_GL_MakeCurrent(m_window, m_glContext) != 0) {
        LOGE("[RenderThread] SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        return;
    }
#else
    // Android: create a new EGL context sharing with the main thread's context
    if (m_eglDisplay == EGL_NO_DISPLAY || m_sharedEglContext == EGL_NO_CONTEXT) {
        LOGE("[RenderThread] Android: EGL display or shared context not set");
        return;
    }

    const EGLint ctxAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    m_eglContext = eglCreateContext(m_eglDisplay, m_eglConfig, m_sharedEglContext, ctxAttribs);
    if (m_eglContext == EGL_NO_CONTEXT) {
        LOGE("[RenderThread/EGL] eglCreateContext FAILED err=0x%x thread=%p", eglGetError(), (void*)pthread_self());
        return;
    }
    LOGI("[RenderThread/EGL] eglCreateContext OK ctx=%p thread=%p", (void*)m_eglContext, (void*)pthread_self());

    LOGI("[RenderThread/EGL] Waiting for surface thread=%p", (void*)pthread_self());
#endif

    std::vector<FrameJob> localJobs;

    while (m_running.load()) {
#ifdef __ANDROID__
        // Process pending surface setup
        {
            ScopedLock surfaceLock(m_queueMutex);
            if (m_pendingWindow && m_eglSurface == EGL_NO_SURFACE) {
                EGLSurface newSurface = eglCreateWindowSurface(
                    m_eglDisplay, m_eglConfig, m_pendingWindow, nullptr);
                if (newSurface != EGL_NO_SURFACE) {
                    if (m_eglSurface != EGL_NO_SURFACE) {
                        eglDestroySurface(m_eglDisplay, m_eglSurface);
                    }
                    m_eglSurface = newSurface;
                    m_surfaceWidth = m_pendingWidth;
                    m_surfaceHeight = m_pendingHeight;
                    LOGI("[RenderThread/EGL] Surface created %dx%d thread=%p",
                         m_surfaceWidth, m_surfaceHeight, (void*)pthread_self());

                    // Recreate composited FBO for new size
                    createCompositedFBO(m_surfaceWidth, m_surfaceHeight);
                }
            }
        }

        // Need surface to render
        if (m_eglSurface == EGL_NO_SURFACE) {
            processGPUCommands();
            {
                ScopedLock lock(m_queueMutex);
                if (m_jobQueue.empty() && m_gpuCommands.empty() && m_running.load()) {
                    m_workCond.wait(m_queueMutex);
                }
            }
            continue;
        }

        // Make current with surface
        if (eglGetCurrentSurface(EGL_DRAW) != m_eglSurface) {
            if (!eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext)) {
                LOGE("[RenderThread] Android eglMakeCurrent failed: 0x%x", eglGetError());
                usleep(16000);
                continue;
            }
        }
#endif

        // Process GPU commands
        processGPUCommands();

        // Wait for frame work
        {
            ScopedLock lock(m_queueMutex);
            while (m_jobQueue.empty() && m_gpuCommands.empty() && m_running.load()) {
                m_workCond.wait(m_queueMutex);
            }
            if (!m_jobQueue.empty()) {
                localJobs.swap(m_jobQueue);
                m_pendingWakeup = false;
            }
        }

        if (localJobs.empty()) {
            continue;
        }

        // Process all pending frame jobs
        for (auto& job : localJobs) {
            auto tj0 = std::chrono::steady_clock::now();

            std::stable_sort(job.commands.begin(), job.commands.end(),
                [](const CompositeCommand& a, const CompositeCommand& b) {
                    return a.zOrder < b.zOrder;
                });

            // Bind target framebuffer and clear
            // Android: composite into off-screen buffer first (matches original single-threaded behavior)
            // Desktop: render directly to screen (fbo=0)
#ifdef __ANDROID__
            if (m_compositedFbo) {
                glBindFramebuffer(GL_FRAMEBUFFER, m_compositedFbo);
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
#else
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);

            // Process each canvas composite
            for (auto& cmd : job.commands) {
                CanvasRenderingContext2d* ctx = cmd.context;
                if (!ctx) continue;

                int cw = cmd.srcW;
                int ch = cmd.srcH;
                if (cw <= 0 || ch <= 0) continue;

                ensureCanvasResources(ctx, cw, ch);

                auto resIt = m_canvasResources.find(ctx);
                if (resIt == m_canvasResources.end() || !resIt->second.nvgCtx) continue;

                auto& res = resIt->second;

                { PERF_SCOPE("replayOnThread");
                ctx->replayOnThread((NVGcontext*)res.nvgCtx, res.fbo, cw, ch);
                }

                    unsigned int dpyRenderFbo = ctx->renderFboId();
                    if (dpyRenderFbo) {
                        glBindFramebuffer(GL_READ_FRAMEBUFFER, dpyRenderFbo);
#ifdef __ANDROID__
                        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_compositedFbo);
#else
                        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
#endif
                        int bSrcX = cmd.srcX, bSrcY = cmd.srcY, bSrcW = cmd.srcW, bSrcH = cmd.srcH;
                        int bDstX = cmd.dstX, bDstY = cmd.dstY;
                        int bDstW = cmd.dstW, bDstH = cmd.dstH;
                        if (cmd.dirtyW > 0 && cmd.dirtyH > 0) {
                            bSrcX = cmd.dirtyX; bSrcY = cmd.dirtyY;
                            bSrcW = cmd.dirtyW; bSrcH = cmd.dirtyH;
                            bDstX = cmd.dstX + (cmd.dirtyX - cmd.srcX);
                            bDstY = cmd.dstY + (cmd.dirtyY - cmd.srcY);
                            bDstW = cmd.dirtyW; bDstH = cmd.dirtyH;
                        }
#ifdef __ANDROID__
                        int glSrcY0 = ch - bSrcY - bSrcH;
                        int glDstY0 = m_surfaceHeight - bDstY - bDstH;
                        glBlitFramebuffer(
                            bSrcX, bSrcY, bSrcX + bSrcW, bSrcY + bSrcH,
                            bDstX, glDstY0, bDstX + bDstW, glDstY0 + bDstH,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
#else
                        int glDstY = cmd.targetH - bDstY - bDstH;
                        glBlitFramebuffer(
                            bSrcX, bSrcY, bSrcX + bSrcW, bSrcY + bSrcH,
                            bDstX, glDstY, bDstX + bDstW, glDstY + bDstH,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
#endif
                    }
            }

#ifdef __ANDROID__
            // Blit composited FBO to window surface
            if (m_compositedFbo) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, m_compositedFbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, m_surfaceWidth, m_surfaceHeight,
                                  0, 0, m_surfaceWidth, m_surfaceHeight,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            // Swap buffers on Android
            eglSwapBuffers(m_eglDisplay, m_eglSurface);
#else
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif

            GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();
            {
                ScopedLock lock(m_completeMutex);
                if (fence) m_frameFences[job.frameId] = reinterpret_cast<void*>(fence);
                m_lastCompletedFrame.store(job.frameId);
                m_completeCond.broadcast();
            }
        }

        localJobs.clear();
    }

    // Cleanup
#ifdef __ANDROID__
    destroyCompositedFBO();
#endif

    for (auto& [ctx, res] : m_canvasResources) {
        if (res.nvgCtx) {
#ifdef __ANDROID__
            nvgDeleteGLES3((NVGcontext*)res.nvgCtx);
#else
            nvgDeleteGL3((NVGcontext*)res.nvgCtx);
#endif
        }
        if (res.fbo) glDeleteFramebuffers(1, (GLuint*)&res.fbo);
        if (res.tex) glDeleteTextures(1, (GLuint*)&res.tex);
        if (res.rb) glDeleteRenderbuffers(1, (GLuint*)&res.rb);
    }
    m_canvasResources.clear();

#ifndef __ANDROID__
    SDL_GL_DeleteContext(m_glContext);
    m_glContext = nullptr;
#else
    if (m_eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(m_eglDisplay, m_eglSurface);
        m_eglSurface = EGL_NO_SURFACE;
    }
    if (m_eglContext != EGL_NO_CONTEXT) {
        eglDestroyContext(m_eglDisplay, m_eglContext);
        m_eglContext = EGL_NO_CONTEXT;
    }
    if (m_pendingWindow) {
        ANativeWindow_release(m_pendingWindow);
        m_pendingWindow = nullptr;
    }
    LOGI("[RenderThread] Android render thread stopped");
#endif
}

} // namespace canvas