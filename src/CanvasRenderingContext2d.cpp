#include "Context.h"
#include "Bindings.h"
#include "Canvas.h"
#include "Perf.h"
#include "quickjs.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <unistd.h>


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace canvas
{
   std::map<std::string, std::string> CanvasRenderingContext2d::s_fontPathCache;
std::vector<CanvasRenderingContext2d::CachedGL> CanvasRenderingContext2d::s_glCache;

   IMPL_QUICKJS_CLASS(CanvasRenderingContext2d);

   void js_CanvasRenderingContext2d_finalizer(JSRuntime* rt, JSValue val) {
       CanvasRenderingContext2d* obj = fromJS<CanvasRenderingContext2d>(val);
       if (!obj) return;
       obj->freeJsFields(rt);
       delete obj;
   }

   // ============================================================================
   // Composite op mapping
   // ============================================================================

   int CanvasRenderingContext2d::mapCompositeOp(std::string const& op) {
      if (op == "source-over") return NVG_SOURCE_OVER;
      if (op == "source-in") return NVG_SOURCE_IN;
      if (op == "source-out") return NVG_SOURCE_OUT;
      if (op == "source-atop") return NVG_ATOP;
      if (op == "destination-over") return NVG_DESTINATION_OVER;
      if (op == "destination-in") return NVG_DESTINATION_IN;
      if (op == "destination-out") return NVG_DESTINATION_OUT;
      if (op == "destination-atop") return NVG_DESTINATION_ATOP;
      if (op == "lighter") return NVG_LIGHTER;
      if (op == "copy") return NVG_COPY;
      if (op == "xor") return NVG_XOR;
      return NVG_SOURCE_OVER;
   }

   // ============================================================================
   // Constructor/Destructor
   // ============================================================================

    CanvasRenderingContext2d::CanvasRenderingContext2d(int width, int height)
       : m_parser(),
         m_width(width),
         m_height(height)
    {
       // Reuse cached GL resources (avoids expensive nvgCreateGL3 shader compile)
       if (!s_glCache.empty()) {
           auto c = s_glCache.back();
           s_glCache.pop_back();
           m_nvgCtx = c.nvgCtx;
           m_fbo = c.fbo;
           m_tex = c.tex;
           m_rb = c.rb;
           if (c.width != width || c.height != height) {
               deleteFbo();
               createFbo(width, height);
           } else {
               bindFbo();
               glClearColor(0, 0, 0, 0);
               glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            m_fontHandleCache = std::move(c.fontCache);
        }
        if (!m_nvgCtx) {
#ifdef __ANDROID__
           m_nvgCtx = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#else
           m_nvgCtx = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#endif
           createFbo(width, height);
        }

       m_tiledCanvas.setSize(width, height);
       m_stateStack.push_back(State());
       m_clipActive = false;

       initDefaultFont();
    }

     CanvasRenderingContext2d::~CanvasRenderingContext2d()
     {
        if (m_nvgCtx && m_fbo && (int)s_glCache.size() < kMaxCacheSize) {
           s_glCache.push_back({m_nvgCtx, m_fbo, m_tex, m_rb, m_width, m_height, std::move(m_fontHandleCache)});
           m_nvgCtx = nullptr;
           m_fbo = m_tex = m_rb = 0;
        }
        deleteFbo();
        if (m_nvgCtx) {
#ifdef __ANDROID__
           nvgDeleteGLES3(m_nvgCtx);
#else
           nvgDeleteGL3(m_nvgCtx);
#endif
        }
#ifndef __ANDROID__
        // Clean up render thread resources for this canvas (desktop only)
        if (RenderThread* rt = RenderThread::instance()) {
            GPUCommand cmd;
            cmd.type = GPUCommandType::CleanupCanvas;
            cmd.cleanupCtx = this;
            auto future = cmd.simplePromise.get_future();
            rt->sendGPUCommand(std::move(cmd));
            future.get();
        }
#endif
     }

   void CanvasRenderingContext2d::initDefaultFont() {
      // Only load font path once (shared across contexts)
      if (s_fontPathCache.find("sans-serif") == s_fontPathCache.end()) {
         const char* candidates[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/System/Library/Fonts/Helvetica.ttf",
            nullptr
         };
         std::string fontPath;
         for (int i = 0; candidates[i]; i++) {
            if (access(candidates[i], R_OK) == 0) {
               fontPath = candidates[i];
               break;
            }
         }
         if (fontPath.empty()) {
            FILE* fp = popen("fc-match --format=%{file} sans-serif 2>/dev/null", "r");
            if (fp) {
               char buf[512];
               if (fgets(buf, sizeof(buf), fp)) {
                  std::string result(buf);
                  if (!result.empty()) {
                     result.erase(result.find_last_not_of(" \n\r\t") + 1);
                     if (access(result.c_str(), R_OK) == 0)
                        fontPath = result;
                  }
               }
               pclose(fp);
            }
         }
         if (!fontPath.empty()) {
            s_fontPathCache["sans-serif"] = fontPath;
            // Try bold variant path
            size_t dot = fontPath.rfind('.');
            if (dot != std::string::npos) {
               std::string base = fontPath.substr(0, dot);
               const char* suffixes[] = {"-Bold", "Bold", "-bold", "_bold", nullptr};
               for (int i = 0; suffixes[i]; i++) {
                  std::string bp = base + suffixes[i] + fontPath.substr(dot);
                  if (access(bp.c_str(), R_OK) == 0) {
                     s_fontPathCache["sans-serif-bold"] = bp;
                     break;
                  }
               }
            }
         }
      }

      // Load font into this NVG context
      auto fhit = m_fontHandleCache.find("sans-serif");
      if (fhit == m_fontHandleCache.end()) {
         auto it = s_fontPathCache.find("sans-serif");
         if (it != s_fontPathCache.end()) {
            int handle = nvgCreateFont(m_nvgCtx, "sans-serif", it->second.c_str());
            if (handle >= 0) {
               m_fontHandleCache["sans-serif"] = handle;
            }
            // Bold variant
            auto bit = s_fontPathCache.find("sans-serif-bold");
            if (bit != s_fontPathCache.end()) {
               int bh = nvgCreateFont(m_nvgCtx, "sans-serif-bold", bit->second.c_str());
               if (bh >= 0) {
                  m_fontHandleCache["sans-serif-bold"] = bh;
                  fprintf(stderr, "[FONT] nvgCreateFont('sans-serif-bold')=%d\n", bh);
               }
            }
         } else {
            fprintf(stderr, "[FONT] initDefaultFont: no font path found!\n");
         }
      }
   }

   int CanvasRenderingContext2d::getFontHandle(const std::string& family, const std::string& weight) {
       std::string key = family + "|" + weight;

       // Per-context cache hit
       auto localIt = m_fontHandleCache.find(key);
       if (localIt != m_fontHandleCache.end()) return localIt->second;

       // Load font into this NVG context
       std::string fontPath;
       auto pathIt = s_fontPathCache.find(key);
       if (pathIt == s_fontPathCache.end()) {
           // Lazily find font path via fontconfig
           std::string fcQuery = family;
           if (!weight.empty() && weight != "normal")
               fcQuery += ":weight=" + weight;
           std::string cmd = "fc-match --format=%{file} '" + fcQuery + "' 2>/dev/null";
           FILE* fp = popen(cmd.c_str(), "r");
           if (fp) {
               char buf[512];
               if (fgets(buf, sizeof(buf), fp)) {
                   std::string result(buf);
                   result.erase(result.find_last_not_of(" \n\r\t") + 1);
                   if (!result.empty() && access(result.c_str(), R_OK) == 0)
                       fontPath = result;
               }
               pclose(fp);
           }
           if (!fontPath.empty())
               s_fontPathCache[key] = fontPath;
       } else {
           fontPath = pathIt->second;
       }

       int handle = -1;
       if (!fontPath.empty()) {
           std::string nvgName = "__font_" + key;
           handle = nvgCreateFont(m_nvgCtx, nvgName.c_str(), fontPath.c_str());
       }

       if (handle < 0) {
           // Fallback to sans-serif
           auto fb = m_fontHandleCache.find("sans-serif");
           handle = (fb != m_fontHandleCache.end()) ? fb->second : -1;
       }
       if (handle >= 0)
           m_fontHandleCache[key] = handle;
       return handle;
    }

   // ============================================================================
   // setSize
   // ============================================================================

   void CanvasRenderingContext2d::setSize(int width, int height) {
      if (width == m_width && height == m_height) return;
      bubbleDirty();

      deleteFbo();
      createFbo(width, height);
      m_width = width;
      m_height = height;
      m_tiledCanvas.setSize(width, height);
      m_tiledCanvas.markAllDirty();
   }

   uint32_t* CanvasRenderingContext2d::getPixels() {
      if (m_width <= 0 || m_height <= 0) return nullptr;
      m_readback.resize((size_t)m_width * m_height);
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
      // Read as BGRA so each uint32_t is ARGB on little-endian
      glReadPixels(0, 0, m_width, m_height, GL_BGRA, GL_UNSIGNED_BYTE, m_readback.data());
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
      return m_readback.data();
   }

   // ============================================================================
   // State application helpers
   // ============================================================================

   void CanvasRenderingContext2d::applyTransformToNVG() {
      nvgResetTransform(m_nvgCtx);
      nvgTransform(m_nvgCtx, m_currentXform[0], m_currentXform[1],
                   m_currentXform[2], m_currentXform[3],
                   m_currentXform[4], m_currentXform[5]);
   }

    void CanvasRenderingContext2d::applyFillStyleToNVG() {
       State& s = currentState();
       nvgGlobalCompositeOperation(m_nvgCtx, mapCompositeOp(s.globalCompositeOperation));
       nvgGlobalAlpha(m_nvgCtx, s.globalAlpha);

       if (s.hasFillGradient) {
          applyFillGradientToNVG(s.fillGradientData, s.globalAlpha);
       } else {
          nvgFillColor(m_nvgCtx, nvgRGBAf(s.fillStyle.r, s.fillStyle.g, s.fillStyle.b,
                                           s.fillStyle.a * s.globalAlpha));
       }
    }

    void CanvasRenderingContext2d::applyStrokeStyleToNVG() {
       State& s = currentState();
       nvgGlobalCompositeOperation(m_nvgCtx, mapCompositeOp(s.globalCompositeOperation));
       nvgGlobalAlpha(m_nvgCtx, s.globalAlpha);
       nvgStrokeWidth(m_nvgCtx, s.lineWidth);
       nvgLineCap(m_nvgCtx, s.nvgLineCap());
       nvgLineJoin(m_nvgCtx, s.nvgLineJoin());

       if (s.hasStrokeGradient) {
          applyStrokeGradientToNVG(s.strokeGradientData, s.globalAlpha);
       } else {
          nvgStrokeColor(m_nvgCtx, nvgRGBAf(s.strokeStyle.r, s.strokeStyle.g, s.strokeStyle.b,
                                              s.strokeStyle.a * s.globalAlpha));
       }
    }

   void CanvasRenderingContext2d::applyStateToNVG() {
      applyTransformToNVG();
      applyFillStyleToNVG();
      applyStrokeStyleToNVG();

      if (m_clipActive) {
         nvgScissor(m_nvgCtx, m_clipX, m_clipY, m_clipW, m_clipH);
      } else {
         nvgResetScissor(m_nvgCtx);
      }
   }

   // Transform a point from user space to device space using the given matrix
   static void transformPoint(float& dx, float& dy, const float* xform, float sx, float sy) {
      dx = sx * xform[0] + sy * xform[2] + xform[4];
      dy = sx * xform[1] + sy * xform[3] + xform[5];
   }

   void CanvasRenderingContext2d::applyFillGradientToNVG(const GradientData& grad, float globalAlpha) {
      if (grad.stops.size() < 2) {
         nvgFillColor(m_nvgCtx, nvgRGBAf(0,0,0,0));
         return;
      }
      auto& first = grad.stops.front();
      auto& last = grad.stops.back();
      NVGcolor c1 = nvgRGBAf(first.color.r, first.color.g, first.color.b, first.color.a * globalAlpha);
      NVGcolor c2 = nvgRGBAf(last.color.r, last.color.g, last.color.b, last.color.a * globalAlpha);
      if (grad.type == GradientData::RADIAL) {
         NVGpaint pt = nvgRadialGradient(m_nvgCtx, grad.x0, grad.y0, grad.r0, grad.r1, c1, c2);
         nvgFillPaint(m_nvgCtx, pt);
      } else {
         NVGpaint pt = nvgLinearGradient(m_nvgCtx, grad.x0, grad.y0, grad.x1, grad.y1, c1, c2);
         nvgFillPaint(m_nvgCtx, pt);
      }
   }

   void CanvasRenderingContext2d::applyStrokeGradientToNVG(const GradientData& grad, float globalAlpha) {
      if (grad.stops.size() < 2) {
         nvgStrokeColor(m_nvgCtx, nvgRGBAf(0,0,0,0));
         return;
      }
      auto& first = grad.stops.front();
      auto& last = grad.stops.back();
      NVGcolor c1 = nvgRGBAf(first.color.r, first.color.g, first.color.b, first.color.a * globalAlpha);
      NVGcolor c2 = nvgRGBAf(last.color.r, last.color.g, last.color.b, last.color.a * globalAlpha);
      if (grad.type == GradientData::RADIAL) {
         NVGpaint pt = nvgRadialGradient(m_nvgCtx, grad.x0, grad.y0, grad.r0, grad.r1, c1, c2);
         nvgStrokePaint(m_nvgCtx, pt);
      } else {
         NVGpaint pt = nvgLinearGradient(m_nvgCtx, grad.x0, grad.y0, grad.x1, grad.y1, c1, c2);
         nvgStrokePaint(m_nvgCtx, pt);
      }
   }

   void CanvasRenderingContext2d::applyFontToNVG() {
      State& state = currentState();
      const std::string& fontStr = state.font;
      if (fontStr != state.cachedFontString) {
         state.cachedFontString = fontStr;
         parseCssFont(fontStr, state.cachedFamily, state.cachedWeight, state.cachedSize);
         // fprintf(stderr, "[FONT] parseCssFont('%s') => family='%s' weight='%s' size=%.1f\n",
         //         fontStr.c_str(), state.cachedFamily.c_str(), state.cachedWeight.c_str(), state.cachedSize);
      }
      nvgFontSize(m_nvgCtx, state.cachedSize);
      nvgTextAlign(m_nvgCtx, state.nvgTextAlign());
      int handle = getFontHandle(state.cachedFamily, state.cachedWeight);
      // fprintf(stderr, "[FONT] applyFontToNVG: handle=%d\n", handle);
      if (handle >= 0) nvgFontFaceId(m_nvgCtx, handle);
   }

    static void sampleGradientAt(float t, const GradientData& grad, float globalAlpha, Color& out) {
        if (t <= grad.stops.front().offset) {
            out = grad.stops.front().color;
            out.a *= globalAlpha;
            return;
        }
        if (t >= grad.stops.back().offset) {
            out = grad.stops.back().color;
            out.a *= globalAlpha;
            return;
        }
        for (size_t j = 0; j + 1 < grad.stops.size(); j++) {
            float s0 = grad.stops[j].offset;
            float s1 = grad.stops[j+1].offset;
            if (t >= s0 && t <= s1) {
                float lt = (t - s0) / (s1 - s0);
                const auto& c0 = grad.stops[j].color;
                const auto& c1 = grad.stops[j+1].color;
                out.r = c0.r + (c1.r - c0.r) * lt;
                out.g = c0.g + (c1.g - c0.g) * lt;
                out.b = c0.b + (c1.b - c0.b) * lt;
                out.a = (c0.a + (c1.a - c0.a) * lt) * globalAlpha;
                return;
            }
        }
        out = grad.stops.back().color;
        out.a *= globalAlpha;
    }

    static int createMultiStopGradientTexture(NVGcontext* vg, const GradientData& grad, float globalAlpha) {
        uint8_t tex[256 * 4];
        for (int i = 0; i < 256; i++) {
            float t = i / 255.0f;
            Color c;
            sampleGradientAt(t, grad, globalAlpha, c);
            tex[i*4+0] = (uint8_t)(std::max(0.0f, std::min(1.0f, c.r)) * 255.0f);
            tex[i*4+1] = (uint8_t)(std::max(0.0f, std::min(1.0f, c.g)) * 255.0f);
            tex[i*4+2] = (uint8_t)(std::max(0.0f, std::min(1.0f, c.b)) * 255.0f);
            tex[i*4+3] = (uint8_t)(std::max(0.0f, std::min(1.0f, c.a)) * 255.0f);
        }
        return nvgCreateImageRGBA(vg, 256, 1, 0, tex);
    }

    // Pushes gradient texture handles to tempImages for cleanup
    static void replayApplyFillStyle(NVGcontext* vg, std::vector<int>& tempImages,
                                      bool hasGrad, const GradientData& grad,
                                      float r, float g, float b, float a, float globalAlpha) {
       if (hasGrad) {
          if (grad.stops.size() < 2) {
             nvgFillColor(vg, nvgRGBAf(0,0,0,0));
             return;
          }
          if (grad.stops.size() == 2 || grad.type == GradientData::RADIAL) {
              auto& first = grad.stops.front();
              auto& last = grad.stops.back();
              NVGcolor c1 = nvgRGBAf(first.color.r, first.color.g, first.color.b, first.color.a * globalAlpha);
              NVGcolor c2 = nvgRGBAf(last.color.r, last.color.g, last.color.b, last.color.a * globalAlpha);
              if (grad.type == GradientData::RADIAL) {
                 nvgFillPaint(vg, nvgRadialGradient(vg, grad.x0, grad.y0, grad.r0, grad.r1, c1, c2));
              } else {
                 nvgFillPaint(vg, nvgLinearGradient(vg, grad.x0, grad.y0, grad.x1, grad.y1, c1, c2));
              }
              return;
          }
          int img = createMultiStopGradientTexture(vg, grad, globalAlpha);
          tempImages.push_back(img);
          float dx = grad.x1 - grad.x0;
          float dy = grad.y1 - grad.y0;
          float d = sqrtf(dx*dx + dy*dy);
          if (d > 0.0001f) { dx /= d; dy /= d; }
          else { dx = 0; dy = 1; }
          float angle = atan2f(dy, dx);
          NVGpaint pt = nvgImagePattern(vg, grad.x0, grad.y0, d, 1.0f, angle, img, 1.0f);
          nvgFillPaint(vg, pt);
          return;
       }
       nvgFillColor(vg, nvgRGBAf(r, g, b, a * globalAlpha));
    }

    static void replayApplyStrokeStyle(NVGcontext* vg, std::vector<int>& tempImages,
                                        bool hasGrad, const GradientData& grad,
                                        float r, float g, float b, float a, float globalAlpha) {
       if (hasGrad) {
          if (grad.stops.size() < 2) {
             nvgStrokeColor(vg, nvgRGBAf(0,0,0,0));
             return;
          }
          if (grad.stops.size() == 2 || grad.type == GradientData::RADIAL) {
              auto& first = grad.stops.front();
              auto& last = grad.stops.back();
              NVGcolor c1 = nvgRGBAf(first.color.r, first.color.g, first.color.b, first.color.a * globalAlpha);
              NVGcolor c2 = nvgRGBAf(last.color.r, last.color.g, last.color.b, last.color.a * globalAlpha);
              if (grad.type == GradientData::RADIAL) {
                 nvgStrokePaint(vg, nvgRadialGradient(vg, grad.x0, grad.y0, grad.r0, grad.r1, c1, c2));
              } else {
                 nvgStrokePaint(vg, nvgLinearGradient(vg, grad.x0, grad.y0, grad.x1, grad.y1, c1, c2));
              }
              return;
          }
          int img = createMultiStopGradientTexture(vg, grad, globalAlpha);
          tempImages.push_back(img);
          float dx = grad.x1 - grad.x0;
          float dy = grad.y1 - grad.y0;
          float d = sqrtf(dx*dx + dy*dy);
          if (d > 0.0001f) { dx /= d; dy /= d; }
          else { dx = 0; dy = 1; }
          float angle = atan2f(dy, dx);
          NVGpaint pt = nvgImagePattern(vg, grad.x0, grad.y0, d, 1.0f, angle, img, 1.0f);
          nvgStrokePaint(vg, pt);
          return;
       }
       nvgStrokeColor(vg, nvgRGBAf(r, g, b, a * globalAlpha));
    }

    // ============================================================================
    // DisplayList replay — walk recorded commands and render via nanovg_gl
    // (assumes the canvas FBO is already bound)
    // ============================================================================

    void CanvasRenderingContext2d::replayDisplayList(std::vector<RecordedCommand>* cmdBuf) {
       PERF_SCOPE("replayDisplayList");
       auto* buf = cmdBuf ? cmdBuf : m_displayList.front();
       if (!buf || buf->empty()) return;
       const auto& cmds = *buf;

       nvgBeginFrame(m_nvgCtx, (float)m_width, (float)m_height, 1.0f);

       float fillR=0,fillG=0,fillB=0,fillA=1;
       float strokeR=0,strokeG=0,strokeB=0,strokeA=1;
       bool hasFillGrad=false,hasStrokeGrad=false;
       GradientData fillGrad, strokeGrad;
        float globalAlpha=1.0f;
        float shadowBlur=0, shadowOffsetX=0, shadowOffsetY=0;
        NVGcolor shadowColor = nvgRGBAf(0,0,0,0);
        struct ReplaySavedState {
            float fillR, fillG, fillB, fillA;
            float strokeR, strokeG, strokeB, strokeA;
            bool hasFillGrad, hasStrokeGrad;
            GradientData fillGrad, strokeGrad;
            float globalAlpha;
            float shadowBlur, shadowOffsetX, shadowOffsetY;
            NVGcolor shadowColor;
            int replayTextAlign;
            int replayCompositeOp;
            float xf[6];
        };
        std::vector<ReplaySavedState> stateStack;
        float curXform[6] = {1,0,0,1,0,0};

        std::vector<int> tempImages;
        struct FontEntry { int handle; float size; };
        std::map<std::string, FontEntry> replayFontCache;
        bool pathEmpty = true;
        size_t pathStart = 0;
        size_t pathCount = 0;


       int replayTextAlign = NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE;
       int replayCompositeOp = NVG_SOURCE_OVER;

       bool fillStyleSetOnNVG = false;
       float lastFillR = 0, lastFillG = 0, lastFillB = 0, lastFillA = -1;
       bool strokeStyleSetOnNVG = false;
       float lastStrokeR = 0, lastStrokeG = 0, lastStrokeB = 0, lastStrokeA = -1;

      auto shadowActive = [&]() {
          return shadowColor.a > 0.001f && (fabsf(shadowOffsetX) > 0.001f || fabsf(shadowOffsetY) > 0.001f || shadowBlur > 0.001f);
      };
      auto applyShadowOffset = [&]() {
          if (shadowOffsetX == 0 && shadowOffsetY == 0) return;
          float det = curXform[0]*curXform[3] - curXform[2]*curXform[1];
          if (fabsf(det) > 0.0001f) {
              float ux = (curXform[3]*shadowOffsetX + -curXform[2]*shadowOffsetY) / det;
              float uy = (-curXform[1]*shadowOffsetX + curXform[0]*shadowOffsetY) / det;
              nvgTranslate(m_nvgCtx, ux, uy);
          } else {
              nvgTranslate(m_nvgCtx, shadowOffsetX, shadowOffsetY);
          }
      };

      for (size_t idx = 0; idx < cmds.size(); idx++) {
         const auto& c = cmds[idx];
         switch (c.type) {

             case CommandType::SetFillStyle:
                fillR=c.color0.r; fillG=c.color0.g; fillB=c.color0.b; fillA=c.color0.a;
                hasFillGrad = false;
                break;

            case CommandType::SetStrokeStyle:
               strokeR=c.color0.r; strokeG=c.color0.g; strokeB=c.color0.b; strokeA=c.color0.a;
               hasStrokeGrad = false;
               break;

             case CommandType::SetFillGradient:
                fillGrad = c.gradient;
                hasFillGrad = true;
                break;

            case CommandType::SetStrokeGradient:
               strokeGrad = c.gradient;
               hasStrokeGrad = true;
               break;

              case CommandType::SetGlobalAlpha:
                 globalAlpha = c.f0;
                 nvgGlobalAlpha(m_nvgCtx, globalAlpha);
                 break;

              case CommandType::SetShadowBlur:
                 shadowBlur = c.f0;
                 break;
              case CommandType::SetShadowColor:
                 shadowColor = nvgRGBAf(c.color0.r, c.color0.g, c.color0.b, c.color0.a);
                 break;
              case CommandType::SetShadowOffsetX:
                 shadowOffsetX = c.f0;
                 break;
              case CommandType::SetShadowOffsetY:
                 shadowOffsetY = c.f0;
                 break;

              case CommandType::SetGlobalCompositeOperation:
                replayCompositeOp = mapCompositeOp(c.s0);
                nvgGlobalCompositeOperation(m_nvgCtx, replayCompositeOp);
                break;

            case CommandType::SetLineWidth:
               nvgStrokeWidth(m_nvgCtx, c.f0);
               break;

            case CommandType::SetLineCap: {
               int cap = NVG_BUTT;
               if (c.s0 == "round") cap = NVG_ROUND;
               else if (c.s0 == "square") cap = NVG_SQUARE;
               nvgLineCap(m_nvgCtx, cap);
               break;
            }

            case CommandType::SetLineJoin: {
               int join = NVG_MITER;
               if (c.s0 == "round") join = NVG_ROUND;
               else if (c.s0 == "bevel") join = NVG_BEVEL;
               nvgLineJoin(m_nvgCtx, join);
               break;
            }

             case CommandType::SetFont: {
                auto fcIt = replayFontCache.find(c.s0);
                if (fcIt == replayFontCache.end()) {
                    std::string family, weight;
                    float size;
                    parseCssFont(c.s0, family, weight, size);
                    // Use render-thread font cache if on render thread, else main cache
                    int handle;
                    if (m_nvgCtx == m_renderNvgCtx) {
                        handle = getFontHandleForContext(m_nvgCtx, family, weight, m_renderFontHandleCache);
                    } else {
                        handle = getFontHandleForContext(m_nvgCtx, family, weight, m_fontHandleCache);
                    }
                    if (handle >= 0) {
                        replayFontCache[c.s0] = {handle, size};
                        nvgFontSize(m_nvgCtx, size);
                        nvgFontFaceId(m_nvgCtx, handle);
                    }
                } else {
                    nvgFontSize(m_nvgCtx, fcIt->second.size);
                    nvgFontFaceId(m_nvgCtx, fcIt->second.handle);
                }
                break;
             }

            case CommandType::SetTextAlign: {
               replayTextAlign &= ~7;
               if (c.s0 == "center") replayTextAlign |= NVG_ALIGN_CENTER;
               else if (c.s0 == "right" || c.s0 == "end") replayTextAlign |= NVG_ALIGN_RIGHT;
               else replayTextAlign |= NVG_ALIGN_LEFT;
               nvgTextAlign(m_nvgCtx, replayTextAlign);
               break;
            }

            case CommandType::SetTextBaseline: {
               replayTextAlign &= ~120;
               if (c.s0 == "top" || c.s0 == "hanging") replayTextAlign |= NVG_ALIGN_TOP;
               else if (c.s0 == "middle") replayTextAlign |= NVG_ALIGN_MIDDLE;
               else if (c.s0 == "alphabetic") replayTextAlign |= NVG_ALIGN_BASELINE;
               else if (c.s0 == "ideographic" || c.s0 == "bottom") replayTextAlign |= NVG_ALIGN_BOTTOM;
               nvgTextAlign(m_nvgCtx, replayTextAlign);
               break;
            }

             // ── Transforms (NVG handles the matrix, no shadow copy needed) ──
             case CommandType::Scale:
                nvgScale(m_nvgCtx, c.f0, c.f1);
                { float s[6]={c.f0,0,0,c.f1,0,0}; nvgTransformMultiply(curXform, s); }
                break;

             case CommandType::Rotate: {
                nvgRotate(m_nvgCtx, c.f0);
                float cs=cosf(c.f0), sn=sinf(c.f0);
                float s[6]={cs,sn,-sn,cs,0,0}; nvgTransformMultiply(curXform, s);
                break;
             }

             case CommandType::Translate:
                nvgTranslate(m_nvgCtx, c.f0, c.f1);
                { float s[6]={1,0,0,1,c.f0,c.f1}; nvgTransformMultiply(curXform, s); }
                break;

             case CommandType::SetTransform:
                nvgResetTransform(m_nvgCtx);
                nvgTransform(m_nvgCtx, c.f0,c.f1,c.f2,c.f3,c.f4,c.f5);
                curXform[0]=c.f0; curXform[1]=c.f1; curXform[2]=c.f2;
                curXform[3]=c.f3; curXform[4]=c.f4; curXform[5]=c.f5;
                break;

             case CommandType::ResetTransform:
                nvgResetTransform(m_nvgCtx);
                curXform[0]=1; curXform[1]=0; curXform[2]=0;
                curXform[3]=1; curXform[4]=0; curXform[5]=0;
                break;

             // ── State stack ────────────────────────────────────────────
               case CommandType::Save:
                  nvgSave(m_nvgCtx);
                  stateStack.push_back({fillR, fillG, fillB, fillA,
                                        strokeR, strokeG, strokeB, strokeA,
                                        hasFillGrad, hasStrokeGrad,
                                        fillGrad, strokeGrad,
                                        globalAlpha,
                                        shadowBlur, shadowOffsetX, shadowOffsetY,
                                        shadowColor,
                                        replayTextAlign, replayCompositeOp,
                                        {curXform[0],curXform[1],curXform[2],curXform[3],curXform[4],curXform[5]}});
                  break;

               case CommandType::Restore:
                  nvgRestore(m_nvgCtx);
                  if (!stateStack.empty()) {
                      auto& s = stateStack.back();
                      fillR = s.fillR; fillG = s.fillG; fillB = s.fillB; fillA = s.fillA;
                      strokeR = s.strokeR; strokeG = s.strokeG; strokeB = s.strokeB; strokeA = s.strokeA;
                      hasFillGrad = s.hasFillGrad; hasStrokeGrad = s.hasStrokeGrad;
                      fillGrad = s.fillGrad; strokeGrad = s.strokeGrad;
                      globalAlpha = s.globalAlpha;
                      shadowBlur = s.shadowBlur; shadowOffsetX = s.shadowOffsetX; shadowOffsetY = s.shadowOffsetY;
                      shadowColor = s.shadowColor;
                      replayTextAlign = s.replayTextAlign;
                      replayCompositeOp = s.replayCompositeOp;
                      memcpy(curXform, s.xf, sizeof(curXform));
                      stateStack.pop_back();
                  }
                  fillStyleSetOnNVG = false;
                  strokeStyleSetOnNVG = false;
                  break;

              // ── Path building ──────────────────────────────────────────
                case CommandType::BeginPath:
                   nvgBeginPath(m_nvgCtx);
                   pathEmpty = true;
                   pathCount = 0;
                   break;

              case CommandType::ClosePath:
                 nvgClosePath(m_nvgCtx);
                 pathEmpty = false;
                 if (pathCount > 0) pathCount++;
                 break;

              case CommandType::MoveTo:
                 nvgMoveTo(m_nvgCtx, c.f0, c.f1);
                 pathEmpty = false;
                 if (pathCount == 0) pathStart = idx;
                 pathCount++;
                 break;

              case CommandType::LineTo:
                 if (pathEmpty) nvgMoveTo(m_nvgCtx, c.f0, c.f1);
                 else nvgLineTo(m_nvgCtx, c.f0, c.f1);
                 pathEmpty = false;
                 if (pathCount == 0) pathStart = idx;
                 pathCount++;
                 break;

              case CommandType::QuadraticCurveTo:
                 if (pathEmpty) nvgMoveTo(m_nvgCtx, c.f0, c.f1);
                 nvgQuadTo(m_nvgCtx, c.f0, c.f1, c.f2, c.f3);
                 pathEmpty = false;
                 if (pathCount == 0) pathStart = idx;
                 pathCount++;
                 break;

              case CommandType::BezierCurveTo:
                 if (pathEmpty) nvgMoveTo(m_nvgCtx, c.f0, c.f1);
                 nvgBezierTo(m_nvgCtx, c.f0,c.f1,c.f2,c.f3,c.f4,c.f5);
                 pathEmpty = false;
                 if (pathCount == 0) pathStart = idx;
                 pathCount++;
                 break;

              case CommandType::ArcTo:
                 nvgArcTo(m_nvgCtx, c.f0,c.f1,c.f2,c.f3,c.f4);
                 pathEmpty = false;
                 if (pathCount == 0) pathStart = idx;
                 pathCount++;
                 break;

              case CommandType::Rect:
                 nvgRect(m_nvgCtx, c.f0, c.f1, c.f2, c.f3);
                 pathEmpty = false;
                 if (pathCount == 0) pathStart = idx;
                 pathCount++;
                 break;

              case CommandType::RoundRect:
                 nvgRoundedRect(m_nvgCtx, c.f0,c.f1,c.f2,c.f3,c.f4);
                 pathEmpty = false;
                 if (pathCount == 0) pathStart = idx;
                 pathCount++;
                 break;

               case CommandType::Arc: {
                       int dir = c.i0 ? NVG_CCW : NVG_CW;
                 nvgArc(m_nvgCtx, c.f0,c.f1,c.f2,c.f3,c.f4,dir);
                   pathEmpty = false;
                   if (pathCount == 0) pathStart = idx;
                   pathCount++;
                   break;
               }

              case CommandType::Ellipse: {
                 bool ccw = c.i0 != 0;
                 bool full = (fabsf(c.f6 - c.f5) >= 2*M_PI - 0.01f);
                 if (full && fabsf(c.f4) < 0.01f) {
                    nvgEllipse(m_nvgCtx, c.f0,c.f1,c.f2,c.f3);
                 } else {
                    nvgSave(m_nvgCtx);
                    nvgTranslate(m_nvgCtx, c.f0, c.f1);
                    nvgRotate(m_nvgCtx, c.f4);
                    nvgScale(m_nvgCtx, c.f2, c.f3);
                    nvgArc(m_nvgCtx, 0,0,1.0f, c.f5,c.f6, ccw ? NVG_CCW : NVG_CW);
                    nvgRestore(m_nvgCtx);
                 }
                 pathEmpty = false;
                 if (pathCount == 0) pathStart = idx;
                 pathCount++;
                 break;
              }

            // ── Drawing ────────────────────────────────────────────────
                case CommandType::Fill: {
                   if (!hasFillGrad && fillStyleSetOnNVG &&
                       fillR == lastFillR && fillG == lastFillG && fillB == lastFillB &&
                       fabsf(fillA * globalAlpha - lastFillA) < 0.001f) {
                   } else {
                      replayApplyFillStyle(m_nvgCtx,tempImages,hasFillGrad,fillGrad,fillR,fillG,fillB,fillA,globalAlpha);
                      if (!hasFillGrad) {
                         fillStyleSetOnNVG = true;
                         lastFillR = fillR; lastFillG = fillG; lastFillB = fillB;
                         lastFillA = fillA * globalAlpha;
                      } else {
                         fillStyleSetOnNVG = false;
                      }
                   }
                   nvgFill(m_nvgCtx);
                   break;
                }

               case CommandType::Stroke: {
                 if (!hasStrokeGrad && strokeStyleSetOnNVG &&
                     strokeR == lastStrokeR && strokeG == lastStrokeG && strokeB == lastStrokeB &&
                     fabsf(strokeA * globalAlpha - lastStrokeA) < 0.001f) {
                 } else {
                    replayApplyStrokeStyle(m_nvgCtx,tempImages,hasStrokeGrad,strokeGrad,strokeR,strokeG,strokeB,strokeA,globalAlpha);
                    if (!hasStrokeGrad) {
                       strokeStyleSetOnNVG = true;
                       lastStrokeR = strokeR; lastStrokeG = strokeG; lastStrokeB = strokeB;
                       lastStrokeA = strokeA * globalAlpha;
                    } else {
                       strokeStyleSetOnNVG = false;
                    }
                 }
                 nvgStroke(m_nvgCtx);
                 break;
              }

               case CommandType::FillRect: {
                  // Shadow pass (skip for non-source-over blends — 'lighter'
                  // with fullscreen rects would tint the entire canvas)
                  if (shadowActive() && replayCompositeOp == NVG_SOURCE_OVER) {
                     nvgSave(m_nvgCtx);
                     if (shadowOffsetX != 0 || shadowOffsetY != 0)
                         applyShadowOffset();
                     nvgFillColor(m_nvgCtx, shadowColor);
                     nvgBeginPath(m_nvgCtx);
                     nvgRect(m_nvgCtx, c.f0, c.f1, c.f2, c.f3);
                     nvgFill(m_nvgCtx);
                     nvgRestore(m_nvgCtx);
                 }
                 // Main draw
                 if (!hasFillGrad && fillStyleSetOnNVG &&
                     fillR == lastFillR && fillG == lastFillG && fillB == lastFillB &&
                     fabsf(fillA * globalAlpha - lastFillA) < 0.001f) {
                 } else {
                    replayApplyFillStyle(m_nvgCtx,tempImages,hasFillGrad,fillGrad,fillR,fillG,fillB,fillA,globalAlpha);
                    if (!hasFillGrad) {
                       fillStyleSetOnNVG = true;
                       lastFillR = fillR; lastFillG = fillG; lastFillB = fillB;
                       lastFillA = fillA * globalAlpha;
                    } else {
                       fillStyleSetOnNVG = false;
                    }
                 }
                 nvgBeginPath(m_nvgCtx);
                 nvgRect(m_nvgCtx, c.f0, c.f1, c.f2, c.f3);
                 nvgFill(m_nvgCtx);
                 break;
              }

             case CommandType::StrokeRect:
                if (!hasStrokeGrad && strokeStyleSetOnNVG &&
                    strokeR == lastStrokeR && strokeG == lastStrokeG && strokeB == lastStrokeB &&
                    fabsf(strokeA * globalAlpha - lastStrokeA) < 0.001f) {
                } else {
                   replayApplyStrokeStyle(m_nvgCtx,tempImages,hasStrokeGrad,strokeGrad,strokeR,strokeG,strokeB,strokeA,globalAlpha);
                   if (!hasStrokeGrad) {
                      strokeStyleSetOnNVG = true;
                      lastStrokeR = strokeR; lastStrokeG = strokeG; lastStrokeB = strokeB;
                      lastStrokeA = strokeA * globalAlpha;
                   } else {
                      strokeStyleSetOnNVG = false;
                   }
                }
                nvgBeginPath(m_nvgCtx);
                nvgRect(m_nvgCtx, c.f0, c.f1, c.f2, c.f3);
                nvgStroke(m_nvgCtx);
                break;

            case CommandType::ClearRect: {
               // Use NVG path to clear the rect
               int saveOp = replayCompositeOp;
               nvgGlobalCompositeOperation(m_nvgCtx, NVG_COPY);
               nvgBeginPath(m_nvgCtx);
               nvgRect(m_nvgCtx, c.f0, c.f1, c.f2, c.f3);
               nvgFillColor(m_nvgCtx, nvgRGBAf(0,0,0,0));
               nvgFill(m_nvgCtx);
               nvgGlobalCompositeOperation(m_nvgCtx, saveOp);
               break;
            }

            case CommandType::Clear:
               // Use NVG path to clear the whole canvas
               nvgGlobalCompositeOperation(m_nvgCtx, NVG_COPY);
               nvgBeginPath(m_nvgCtx);
               nvgRect(m_nvgCtx, 0, 0, (float)m_width, (float)m_height);
               nvgFillColor(m_nvgCtx, nvgRGBAf(0,0,0,0));
               nvgFill(m_nvgCtx);
               nvgGlobalCompositeOperation(m_nvgCtx, replayCompositeOp);
               break;

            // ── Text ───────────────────────────────────────────────────
              case CommandType::FillText: {
                 // Shadow pass
                 if (shadowActive() && replayCompositeOp == NVG_SOURCE_OVER) {
                    nvgSave(m_nvgCtx);
                    if (shadowOffsetX != 0 || shadowOffsetY != 0)
                        applyShadowOffset();
                    nvgFillColor(m_nvgCtx, shadowColor);
                    nvgText(m_nvgCtx, c.f0, c.f1, c.s0.c_str(), nullptr);
                    nvgRestore(m_nvgCtx);
                }
                // Main draw
                if (!hasFillGrad && fillStyleSetOnNVG &&
                    fillR == lastFillR && fillG == lastFillG && fillB == lastFillB &&
                    fabsf(fillA * globalAlpha - lastFillA) < 0.001f) {
                } else {
                   replayApplyFillStyle(m_nvgCtx,tempImages,hasFillGrad,fillGrad,fillR,fillG,fillB,fillA,globalAlpha);
                   if (!hasFillGrad) {
                      fillStyleSetOnNVG = true;
                      lastFillR = fillR; lastFillG = fillG; lastFillB = fillB;
                      lastFillA = fillA * globalAlpha;
                   } else {
                      fillStyleSetOnNVG = false;
                   }
                }
                nvgText(m_nvgCtx, c.f0, c.f1, c.s0.c_str(), nullptr);
                break;
             }

              case CommandType::StrokeText: {
                 if (shadowActive() && replayCompositeOp == NVG_SOURCE_OVER) {
                    nvgSave(m_nvgCtx);
                    if (shadowOffsetX != 0 || shadowOffsetY != 0)
                        applyShadowOffset();
                    nvgFillColor(m_nvgCtx, shadowColor);
                    nvgText(m_nvgCtx, c.f0, c.f1, c.s0.c_str(), nullptr);
                    nvgRestore(m_nvgCtx);
                }
                if (!hasStrokeGrad && strokeStyleSetOnNVG &&
                    strokeR == lastStrokeR && strokeG == lastStrokeG && strokeB == lastStrokeB &&
                    fabsf(strokeA * globalAlpha - lastStrokeA) < 0.001f) {
                } else {
                   replayApplyStrokeStyle(m_nvgCtx,tempImages,hasStrokeGrad,strokeGrad,strokeR,strokeG,strokeB,strokeA,globalAlpha);
                   if (!hasStrokeGrad) {
                      strokeStyleSetOnNVG = true;
                      lastStrokeR = strokeR; lastStrokeG = strokeG; lastStrokeB = strokeB;
                      lastStrokeA = strokeA * globalAlpha;
                   } else {
                      strokeStyleSetOnNVG = false;
                   }
                }
                nvgText(m_nvgCtx, c.f0, c.f1, c.s0.c_str(), nullptr);
                break;
             }

            // ── Image ──────────────────────────────────────────────────
             case CommandType::DrawImage: {
                if (c.imgWidth <= 0 || c.imgHeight <= 0 || c.pixelData.empty() ||
                    c.f2 <= 0 || c.f3 <= 0)
                   break;
                 int img = nvgCreateImageRGBA(m_nvgCtx, c.imgWidth, c.imgHeight,
                                               0, c.pixelData.data());
                if (img >= 0) {
                   NVGpaint pt = nvgImagePattern(m_nvgCtx, c.f0,c.f1,c.f2,c.f3,0,img,1.0f);
                   nvgBeginPath(m_nvgCtx);
                   nvgRect(m_nvgCtx, c.f0,c.f1,c.f2,c.f3);
                   nvgFillPaint(m_nvgCtx, pt);
                   nvgFill(m_nvgCtx);
                   tempImages.push_back(img);
                }
                break;
            }

              case CommandType::PutImageData: {
                 int pw = (int)c.f2, ph = (int)c.f3;
                 if (pw > 0 && ph > 0 && !c.pixelData.empty()) {
                     // Draw pixel data via NVG image to avoid GL FBO/texture cache issues
                     int img = nvgCreateImageRGBA(m_nvgCtx, pw, ph, 0, c.pixelData.data());
                     if (img >= 0) {
                        nvgGlobalCompositeOperation(m_nvgCtx, NVG_SOURCE_OVER);
                        NVGpaint pt = nvgImagePattern(m_nvgCtx, c.f0, c.f1, (float)pw, (float)ph, 0.0f, img, 1.0f);
                        nvgBeginPath(m_nvgCtx);
                        nvgRect(m_nvgCtx, c.f0, c.f1, (float)pw, (float)ph);
                        nvgFillPaint(m_nvgCtx, pt);
                        nvgFill(m_nvgCtx);
                        tempImages.push_back(img);
                     }
                 }
                 break;
              }

             case CommandType::Clip:
                nvgScissor(m_nvgCtx, 0,0,(float)m_width,(float)m_height);
                break;

          default:
                break;
          }
       }

        nvgEndFrame(m_nvgCtx);
        for (int img : tempImages) nvgDeleteImage(m_nvgCtx, img);
    }

    void CanvasRenderingContext2d::swapDisplayList() {
        m_displayList.swap();
    }

    void CanvasRenderingContext2d::replayOnThread(NVGcontext* renderNvgCtx, int targetFbo, int w, int h) {
        auto* cmdBuf = m_displayList.front();
        if (!cmdBuf || cmdBuf->empty()) return;

        NVGcontext* origCtx = m_nvgCtx;
        m_nvgCtx = renderNvgCtx;
        m_renderNvgCtx = renderNvgCtx;

        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)targetFbo);
        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        replayDisplayList(cmdBuf);

        m_renderNvgCtx = nullptr;
        m_nvgCtx = origCtx;

        // Store the render FBO so the main thread can blit it to screen
        m_renderFbo = (unsigned int)targetFbo;
    }

    int CanvasRenderingContext2d::getFontHandleForContext(
            NVGcontext* nvgCtx, const std::string& family, const std::string& weight,
            std::map<std::string, int>& fontCache) {
        std::string key = family + "|" + weight;
        auto localIt = fontCache.find(key);
        if (localIt != fontCache.end()) return localIt->second;

        std::string fontPath;
        auto pathIt = s_fontPathCache.find(key);
        if (pathIt == s_fontPathCache.end()) {
            std::string fcQuery = family;
            if (!weight.empty() && weight != "normal")
                fcQuery += ":weight=" + weight;
            std::string cmd = "fc-match --format=%{file} '" + fcQuery + "' 2>/dev/null";
            FILE* fp = popen(cmd.c_str(), "r");
            if (fp) {
                char buf[512];
                if (fgets(buf, sizeof(buf), fp)) {
                    std::string result(buf);
                    result.erase(result.find_last_not_of(" \n\r\t") + 1);
                    if (!result.empty() && access(result.c_str(), R_OK) == 0)
                        fontPath = result;
                }
                pclose(fp);
            }
            if (!fontPath.empty())
                s_fontPathCache[key] = fontPath;
        } else {
            fontPath = pathIt->second;
        }

        int handle = -1;
        if (!fontPath.empty()) {
            std::string nvgName = "__font_" + key;
            handle = nvgCreateFont(nvgCtx, nvgName.c_str(), fontPath.c_str());
        }

        if (handle >= 0)
            fontCache[key] = handle;
        return handle;
    }

    // ============================================================================
    // GL FBO helpers
   // ============================================================================

   void CanvasRenderingContext2d::bindFbo() const {
      glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
      glViewport(0, 0, m_width, m_height);
   }

    void CanvasRenderingContext2d::createFbo(int width, int height) {
#ifndef __ANDROID__
       if (RenderThread* rt = RenderThread::instance()) {
           // Send command to render thread, wait for result
           GPUCommand cmd;
           cmd.type = GPUCommandType::CreateFBO;
           cmd.width = width;
           cmd.height = height;
           auto future = cmd.createPromise.get_future();
           rt->sendGPUCommand(std::move(cmd));
           auto result = future.get();
           if (result.success) {
               m_fbo = (unsigned int)result.fboId;
               m_tex = (unsigned int)result.texId;
               m_rb = (unsigned int)result.rbId;
           }
           return;
       }
#endif
       // Fallback: direct GL (single-thread mode)
       glGenTextures(1, &m_tex);
       glBindTexture(GL_TEXTURE_2D, m_tex);
       glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
       glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
       glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
       glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
       glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
       glBindTexture(GL_TEXTURE_2D, 0);

       glGenFramebuffers(1, &m_fbo);
       glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
       glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_tex, 0);
       glGenRenderbuffers(1, &m_rb);
       glBindRenderbuffer(GL_RENDERBUFFER, m_rb);
       glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
       glBindRenderbuffer(GL_RENDERBUFFER, 0);
       glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, m_rb);
       GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
       if (status != GL_FRAMEBUFFER_COMPLETE)
           fprintf(stderr, "[GL] FBO incomplete: 0x%x\n", status);
       glBindFramebuffer(GL_FRAMEBUFFER, 0);

       glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
       glClearColor(0, 0, 0, 0);
       glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
       glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void CanvasRenderingContext2d::deleteFbo() {
#ifndef __ANDROID__
       if (RenderThread* rt = RenderThread::instance()) {
           GPUCommand cmd;
           cmd.type = GPUCommandType::DeleteFBO;
           cmd.fboId = (int)m_fbo;
           cmd.texId = (int)m_tex;
           cmd.rbId = (int)m_rb;
           auto future = cmd.simplePromise.get_future();
           rt->sendGPUCommand(std::move(cmd));
           future.get();  // wait for completion
           m_fbo = m_tex = m_rb = 0;
           return;
       }
#endif
       if (m_rb) { glDeleteRenderbuffers(1, &m_rb); m_rb = 0; }
       if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
       if (m_tex) { glDeleteTextures(1, &m_tex); m_tex = 0; }
    }

   void CanvasRenderingContext2d::blitToFbo(uintptr_t targetFbo, int targetW, int targetH,
                                            int srcX, int srcY, int srcW, int srcH,
                                            int dstX, int dstY, int dstW, int dstH) {
      GLuint target = (GLuint)targetFbo;
      // Clamp source
      if (srcX < 0) { dstW += srcX; dstX -= srcX; srcX = 0; }
      if (srcY < 0) { dstH += srcY; dstY -= srcY; srcY = 0; }
      if (srcX + srcW > m_width) srcW = m_width - srcX;
      if (srcY + srcH > m_height) srcH = m_height - srcY;
      if (srcW <= 0 || srcH <= 0) return;

      if (dstX < 0) { srcW += dstX; srcX -= dstX; dstX = 0; }
      if (dstY < 0) { srcH += dstY; srcY -= dstY; dstY = 0; }
      if (dstX + dstW > targetW) dstW = targetW - dstX;
      if (dstY + dstH > targetH) dstH = targetH - dstY;
      if (dstW <= 0 || dstH <= 0) return;

      // Save current FBO
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

      // Blit from canvas FBO to target FBO, flipping Y
      // The canvas FBO was rendered by NVG (Y-down), so NVG pixel (0,0)
      // is at FBO bottom-left in GL coords, and (0, m_height-1) is at top-right.
      // glBlitFramebuffer uses GL coords (Y-up), so we must flip the source Y.
      glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);

      int glSrcY0 = m_height - srcY - srcH;
      int glDstY0 = targetH - dstY - dstH;

      glBlitFramebuffer(srcX, glSrcY0, srcX + srcW, glSrcY0 + srcH,
                        dstX, glDstY0, dstX + dstW, glDstY0 + dstH,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);

      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
   }

   // ============================================================================
   // composite / copyImageTo
   // ============================================================================

   void CanvasRenderingContext2d::copyImageTo(uintptr_t target) {
      // Not supported in GL backend — use composite instead
      (void)target;
   }

   void CanvasRenderingContext2d::composite(uintptr_t target, int targetW, int targetH) {
      composite(target, targetW, targetH, 0, 0, targetW, targetH);
   }

   void CanvasRenderingContext2d::composite(uintptr_t target, int targetW, int targetH,
                                            int offsetX, int offsetY, int clipW, int clipH) {
      composite(target, targetW, targetH, 0, 0, m_width, m_height,
                offsetX, offsetY, clipW > 0 ? clipW : m_width,
                clipH > 0 ? clipH : m_height);
   }

    // Global render command list for dual-threaded rendering
    // (populated by composite(), consumed by CanvasApp::RenderFrame)
    std::vector<CompositeCommand> g_renderCommands;

    void CanvasRenderingContext2d::composite(uintptr_t target, int targetW, int targetH,
                                              int srcX, int srcY, int srcW, int srcH,
                                              int dstX, int dstY, int dstW, int dstH) {
        if (targetW <= 0 || targetH <= 0) return;

       bubbleDirty();
       m_tiledCanvas.markDirty(srcX, srcY, srcW, srcH);
           m_displayList.swap();

       // Compute dirty region bounds for incremental blit
       int dX, dY, dW, dH;
       m_tiledCanvas.getDirtyBounds(dX, dY, dW, dH);
       // Clamp dirty region to source rect
       if (dX < srcX) { dW -= srcX - dX; dX = srcX; }
       if (dY < srcY) { dH -= srcY - dY; dY = srcY; }
       if (dX + dW > srcX + srcW) dW = srcX + srcW - dX;
       if (dY + dH > srcY + srcH) dH = srcY + srcH - dY;

       // Record composite command for the render thread
       g_renderCommands.push_back({this,
                                    (int)dstX, (int)dstY, (int)dstW, (int)dstH,
                                    (int)srcX, (int)srcY, (int)srcW, (int)srcH,
                                    (int)target, targetW, targetH, 0,
                                    dX, dY, dW, dH});
     }

   // ============================================================================
   // Styles
   // ============================================================================

    float CanvasRenderingContext2d::lineWidth() const { return currentState().lineWidth; }
    void CanvasRenderingContext2d::setLineWidth(float width) {
       currentState().lineWidth = width;
       RecordedCommand cmd;
       cmd.type = CommandType::SetLineWidth;
       cmd.f0 = width;
       m_displayList.append(cmd);
    }
    std::string CanvasRenderingContext2d::lineCap() const { return currentState().lineCap; }
    void CanvasRenderingContext2d::setLineCap(std::string const& cap) {
       currentState().lineCap = cap;
       RecordedCommand cmd;
       cmd.type = CommandType::SetLineCap;
       cmd.s0 = cap;
       m_displayList.append(cmd);
    }
    std::string CanvasRenderingContext2d::lineJoin() const { return currentState().lineJoin; }
    void CanvasRenderingContext2d::setLineJoin(std::string const& join) {
       currentState().lineJoin = join;
       RecordedCommand cmd;
       cmd.type = CommandType::SetLineJoin;
       cmd.s0 = join;
       m_displayList.append(cmd);
    }

   std::string CanvasRenderingContext2d::strokeStyle() const { return currentState().strokeStyleString; }
    void CanvasRenderingContext2d::setStrokeStyle(std::string const& style) {
       State& s = currentState();
       s.strokeStyleString = style;
      s.strokeStyle = m_parser.parse(style);
      s.hasStrokeGradient = false;
      RecordedCommand cmd;
      cmd.type = CommandType::SetStrokeStyle;
      cmd.color0 = s.strokeStyle;
      m_displayList.append(cmd);
   }

   std::string CanvasRenderingContext2d::fillStyle() const {
       const Color& c = currentState().fillStyle;
       int ri = (int)(c.r * 255.0f + 0.5f);
       int gi = (int)(c.g * 255.0f + 0.5f);
       int bi = (int)(c.b * 255.0f + 0.5f);
       char buf[64];
       snprintf(buf, sizeof(buf), "rgba(%d, %d, %d, %.1f)", ri, gi, bi, c.a);
       return std::string(buf);
   }
    void CanvasRenderingContext2d::setFillStyle(std::string const& style) {
       State& s = currentState();
       s.fillStyleString = style;
       s.fillStyle = m_parser.parse(style);
       s.hasFillGradient = false;
       RecordedCommand cmd;
       cmd.type = CommandType::SetFillStyle;
       cmd.color0 = s.fillStyle;
       m_displayList.append(cmd);
    }

   void CanvasRenderingContext2d::setFillGradient(GradientData const& grad) {
      State& s = currentState();
      s.hasFillGradient = true;
      s.fillGradientData = grad;
      s.fillStyleString = "gradient";
      RecordedCommand cmd;
      cmd.type = CommandType::SetFillGradient;
      cmd.gradient = grad;
      m_displayList.append(cmd);
   }

   void CanvasRenderingContext2d::setStrokeGradient(GradientData const& grad) {
      State& s = currentState();
      s.hasStrokeGradient = true;
      s.strokeGradientData = grad;
      s.strokeStyleString = "gradient";
      RecordedCommand cmd;
      cmd.type = CommandType::SetStrokeGradient;
      cmd.gradient = grad;
      m_displayList.append(cmd);
   }

   float CanvasRenderingContext2d::globalAlpha() const { return currentState().globalAlpha; }
   void CanvasRenderingContext2d::setGlobalAlpha(float alpha) {
      currentState().globalAlpha = alpha;
      RecordedCommand cmd;
      cmd.type = CommandType::SetGlobalAlpha;
      cmd.f0 = alpha;
      m_displayList.append(cmd);
   }

   std::string CanvasRenderingContext2d::globalCompositeOperation() const { return currentState().globalCompositeOperation; }
   void CanvasRenderingContext2d::setGlobalCompositeOperation(std::string const& op) {
      currentState().globalCompositeOperation = op;
      RecordedCommand cmd;
      cmd.type = CommandType::SetGlobalCompositeOperation;
      cmd.s0 = op;
      m_displayList.append(cmd);
   }

   // ============================================================================
   // Shadow (stubs)
   // ============================================================================

   float CanvasRenderingContext2d::shadowBlur() const { return currentState().shadowBlur; }
   void CanvasRenderingContext2d::setShadowBlur(float blur) {
      currentState().shadowBlur = blur;
      RecordedCommand cmd;
      cmd.type = CommandType::SetShadowBlur;
      cmd.f0 = blur;
      m_displayList.append(cmd);
   }
    std::string CanvasRenderingContext2d::shadowColor() const { return currentState().shadowColor; }
    void CanvasRenderingContext2d::setShadowColor(std::string const& color) {
       currentState().shadowColor = color;
       RecordedCommand cmd;
       cmd.type = CommandType::SetShadowColor;
       cmd.color0 = m_parser.parse(color);
       m_displayList.append(cmd);
    }
    float CanvasRenderingContext2d::shadowOffsetX() const { return currentState().shadowOffsetX; }
    void CanvasRenderingContext2d::setShadowOffsetX(float x) {
       currentState().shadowOffsetX = x;
       RecordedCommand cmd;
       cmd.type = CommandType::SetShadowOffsetX;
       cmd.f0 = x;
       m_displayList.append(cmd);
    }
    float CanvasRenderingContext2d::shadowOffsetY() const { return currentState().shadowOffsetY; }
    void CanvasRenderingContext2d::setShadowOffsetY(float y) {
       currentState().shadowOffsetY = y;
       RecordedCommand cmd;
       cmd.type = CommandType::SetShadowOffsetY;
       cmd.f0 = y;
       m_displayList.append(cmd);
    }

    // ============================================================================
    // Image smoothing
   // ============================================================================

   bool CanvasRenderingContext2d::imageSmoothingEnabled() const { return currentState().imageSmoothingEnabled; }
   void CanvasRenderingContext2d::setImageSmoothingEnabled(bool enabled) { currentState().imageSmoothingEnabled = enabled; }
   std::string CanvasRenderingContext2d::imageSmoothingQuality() const { return currentState().imageSmoothingQuality; }
   void CanvasRenderingContext2d::setImageSmoothingQuality(std::string const& quality) { currentState().imageSmoothingQuality = quality; }

   // ============================================================================
   // Text properties
   // ============================================================================

    std::string CanvasRenderingContext2d::font() const { return currentState().font; }
    void CanvasRenderingContext2d::setFont(std::string const& fontStr) {
       currentState().font = fontStr;
       RecordedCommand cmd;
       cmd.type = CommandType::SetFont;
       cmd.s0 = fontStr;
       m_displayList.append(cmd);
    }
    std::string CanvasRenderingContext2d::textAlign() const { return currentState().textAlign; }
    void CanvasRenderingContext2d::setTextAlign(std::string const& align) {
       currentState().textAlign = align;
       RecordedCommand cmd;
       cmd.type = CommandType::SetTextAlign;
       cmd.s0 = align;
       m_displayList.append(cmd);
    }
    std::string CanvasRenderingContext2d::textBaseline() const { return currentState().textBaseline; }
    void CanvasRenderingContext2d::setTextBaseline(std::string const& baseline) {
       currentState().textBaseline = baseline;
       RecordedCommand cmd;
       cmd.type = CommandType::SetTextBaseline;
       cmd.s0 = baseline;
       m_displayList.append(cmd);
    }

   void CanvasRenderingContext2d::parseCssFont(std::string const& fontStr, std::string &family,
                                                std::string &weight, float &size) {
      family = "sans-serif";
      weight = "normal";
      size = 12.0f;
      std::string s = fontStr;
      size_t start = s.find_first_not_of(" \t");
      size_t end = s.find_last_not_of(" \t");
      if (start == std::string::npos) return;
      s = s.substr(start, end - start + 1);

      std::vector<std::string> tokens;
      std::istringstream iss(s);
      std::string token;
      while (iss >> token) tokens.push_back(token);

      bool foundSize = false;
      size_t sizeIndex = 0;
      for (size_t i = 0; i < tokens.size(); i++) {
         const std::string& tok = tokens[i];
         if (tok == "bold" || tok == "bolder") { weight = "bold"; continue; }
         if (tok.length() == 3 && isdigit(tok[0])) {
            bool allDigits = true;
            for (char c : tok) if (!isdigit(c)) { allDigits = false; break; }
            if (allDigits) { if (std::stoi(tok) >= 700) weight = "bold"; continue; }
         }
         size_t unitPos = tok.find_first_of("pxptrem%");
         if (unitPos != std::string::npos) {
            std::string unit = tok.substr(unitPos);
            std::string numStr = tok.substr(0, unitPos);
            if (numStr.find_first_not_of("0123456789.") == std::string::npos) {
               float value = std::stof(numStr);
               if (unit == "px") size = value;
               else if (unit == "pt") size = value * 96.0f / 72.0f;
               else if (unit == "em") size = value * 16.0f;
               else if (unit == "rem") size = value * 16.0f;
               else if (unit == "%") size = value / 100.0f * 16.0f;
               else size = value;
               foundSize = true; sizeIndex = i; break;
            }
         }
         if (tok == "italic" || tok == "oblique" || tok == "small-caps" ||
             tok == "normal" || tok == "lighter") continue;
      }

      if (foundSize && sizeIndex + 1 < tokens.size()) {
         std::string familyStr;
         for (size_t i = sizeIndex + 1; i < tokens.size(); i++) {
            if (i > sizeIndex + 1) familyStr += " ";
            familyStr += tokens[i];
         }
         std::string cleanFamily;
         bool inQuote = false;
         char quoteChar = 0;
         for (size_t i = 0; i < familyStr.length(); i++) {
            char c = familyStr[i];
            if (c == '"' || c == '\'') {
               if (inQuote && c == quoteChar) inQuote = false;
               else if (!inQuote) { inQuote = true; quoteChar = c; }
               continue;
            }
            if (!inQuote && c == ',') break;
            cleanFamily += c;
         }
         start = cleanFamily.find_first_not_of(" \t");
         end = cleanFamily.find_last_not_of(" \t");
         if (start != std::string::npos)
            family = cleanFamily.substr(start, end - start + 1);
         else if (!cleanFamily.empty())
            family = cleanFamily;
      }
   }

   // ============================================================================
   // Text methods
   // ============================================================================

   void CanvasRenderingContext2d::fillText(std::string const& text, float x, float y) {
      float tw = (float)text.size() * 8.0f;
      RecordedCommand cmd;
      cmd.type = CommandType::FillText;
      cmd.f0 = x; cmd.f1 = y;
      cmd.s0 = text;
      cmd.hasBounds = true;
      cmd.boundsX = x; cmd.boundsY = y - currentState().cachedSize;
      cmd.boundsW = tw; cmd.boundsH = currentState().cachedSize;
      m_displayList.append(cmd);
      bubbleDirty();
      m_tiledCanvas.markDirty(cmd.boundsX, cmd.boundsY, cmd.boundsW, cmd.boundsH);
   }

   void CanvasRenderingContext2d::strokeText(std::string const& text, float x, float y) {
      float tw = (float)text.size() * 8.0f;
      RecordedCommand cmd;
      cmd.type = CommandType::StrokeText;
      cmd.f0 = x; cmd.f1 = y;
      cmd.s0 = text;
      cmd.hasBounds = true;
      cmd.boundsX = x; cmd.boundsY = y - currentState().cachedSize;
      cmd.boundsW = tw; cmd.boundsH = currentState().cachedSize;
      m_displayList.append(cmd);
      bubbleDirty();
      m_tiledCanvas.markDirty(cmd.boundsX, cmd.boundsY, cmd.boundsW, cmd.boundsH);
   }

   void CanvasRenderingContext2d::measureText(std::string const& text, double &outWidth,
                                               double &outAscent, double &outDescent) {
      applyFontToNVG();
      float bounds[4];
      float advance = nvgTextBounds(m_nvgCtx, 0, 0, text.c_str(), nullptr, bounds);
      outWidth = advance > 0 ? advance : (bounds[2] - bounds[0]);
      float asc, desc, lineH;
      nvgTextMetrics(m_nvgCtx, &asc, &desc, &lineH);
      outAscent = asc;
      outDescent = -desc;
   }

   // ============================================================================
   // Transformations
   // ============================================================================

   void CanvasRenderingContext2d::scale(float x, float y) {
      m_displayList.appendFloat2(CommandType::Scale, x, y);
      float* t = m_currentXform;
      t[0] = t[0] * x;  t[1] = t[1] * y;
      t[2] = t[2] * x;  t[3] = t[3] * y;
      memcpy(currentState().transform, t, sizeof(m_currentXform));
   }

   void CanvasRenderingContext2d::rotate(float angle) {
      m_displayList.appendFloat(CommandType::Rotate, angle);
      float s = sinf(angle), c = cosf(angle);
      float* t = m_currentXform;
      float a = t[0], b = t[1], ct = t[2], d = t[3];
      t[0] = a * c + ct * s;  t[1] = b * c + d * s;
      t[2] = ct * c - a * s;  t[3] = d * c - b * s;
      memcpy(currentState().transform, t, sizeof(m_currentXform));
   }

   void CanvasRenderingContext2d::translate(float x, float y) {
      m_displayList.appendFloat2(CommandType::Translate, x, y);
      float* t = m_currentXform;
      t[4] = t[0] * x + t[2] * y + t[4];
      t[5] = t[1] * x + t[3] * y + t[5];
      memcpy(currentState().transform, t, sizeof(m_currentXform));
   }

   void CanvasRenderingContext2d::setTransform(float a, float b, float c, float d, float e, float f) {
      m_currentXform[0]=a; m_currentXform[1]=b;
      m_currentXform[2]=c; m_currentXform[3]=d;
      m_currentXform[4]=e; m_currentXform[5]=f;
      memcpy(currentState().transform, m_currentXform, sizeof(m_currentXform));
      m_displayList.appendFloat6(CommandType::SetTransform, a, b, c, d, e, f);
   }

   void CanvasRenderingContext2d::resetTransform() {
      memset(m_currentXform, 0, sizeof(m_currentXform));
      m_currentXform[0] = 1; m_currentXform[3] = 1;
      memcpy(currentState().transform, m_currentXform, sizeof(m_currentXform));
      m_displayList.appendSimple(CommandType::ResetTransform);
   }

   // ============================================================================
   // State save/restore
   // ============================================================================

   void CanvasRenderingContext2d::save() {
      memcpy(currentState().transform, m_currentXform, sizeof(m_currentXform));
      m_stateStack.push_back(currentState());
      m_displayList.appendSimple(CommandType::Save);
   }

   void CanvasRenderingContext2d::restore() {
      if (m_stateStack.size() > 1) m_stateStack.pop_back();
      memcpy(m_currentXform, currentState().transform, sizeof(m_currentXform));
      m_displayList.appendSimple(CommandType::Restore);
   }

   // ============================================================================
   // Image drawing
   // ============================================================================

   void CanvasRenderingContext2d::drawImage(ImageData* image, float x, float y,
                                             float width, float height) {
      if (!image) return;

      bubbleDirty();
      m_tiledCanvas.markDirty(x, y, width, height);

      RecordedCommand cmd;
      cmd.type = CommandType::DrawImage;
      cmd.f0 = x; cmd.f1 = y; cmd.f2 = width; cmd.f3 = height;
      cmd.hasBounds = true;
      cmd.boundsX = x; cmd.boundsY = y; cmd.boundsW = width; cmd.boundsH = height;
      cmd.imgWidth = image->width();
      cmd.imgHeight = image->height();
      size_t dataSize = (size_t)cmd.imgWidth * cmd.imgHeight * 4;
      cmd.pixelData.assign(image->data(), image->data() + dataSize);
      m_displayList.append(cmd);
   }

   // ============================================================================
   // Path functions
   // ============================================================================

    void CanvasRenderingContext2d::beginPath() {
       m_displayList.appendSimple(CommandType::BeginPath);
    }

    void CanvasRenderingContext2d::closePath() {
       m_displayList.appendSimple(CommandType::ClosePath);
    }

    void CanvasRenderingContext2d::fill() {
       bubbleDirty();
       RecordedCommand cmd;
       cmd.type = CommandType::Fill;
       cmd.hasBounds = true;
       cmd.boundsX = 0; cmd.boundsY = 0;
       cmd.boundsW = (float)m_width; cmd.boundsH = (float)m_height;
       m_displayList.append(cmd);
       m_tiledCanvas.markDirty(0, 0, (float)m_width, (float)m_height);
    }

   void CanvasRenderingContext2d::stroke() {
      bubbleDirty();
      RecordedCommand cmd;
      cmd.type = CommandType::Stroke;
      cmd.hasBounds = true;
      cmd.boundsX = 0; cmd.boundsY = 0;
      cmd.boundsW = (float)m_width; cmd.boundsH = (float)m_height;
      m_displayList.append(cmd);
      m_tiledCanvas.markDirty(0, 0, (float)m_width, (float)m_height);
   }

   void CanvasRenderingContext2d::clip() {
      m_clipActive = true;
      m_clipX = 0; m_clipY = 0;
      m_clipW = (float)m_width; m_clipH = (float)m_height;
      m_displayList.appendSimple(CommandType::Clip);
   }

   void CanvasRenderingContext2d::moveTo(float x, float y) {
      m_displayList.appendFloat2(CommandType::MoveTo, x, y);
   }

   void CanvasRenderingContext2d::lineTo(float x, float y) {
      m_displayList.appendFloat2(CommandType::LineTo, x, y);
   }

   void CanvasRenderingContext2d::quadraticCurveTo(float cpx, float cpy, float x, float y) {
      m_displayList.appendFloat4(CommandType::QuadraticCurveTo, cpx, cpy, x, y);
   }

   void CanvasRenderingContext2d::bezierCurveTo(float cp1x, float cp1y,
                                                 float cp2x, float cp2y,
                                                 float x, float y) {
      RecordedCommand cmd;
      cmd.type = CommandType::BezierCurveTo;
      cmd.f0 = cp1x; cmd.f1 = cp1y; cmd.f2 = cp2x;
      cmd.f3 = cp2y; cmd.f4 = x; cmd.f5 = y;
      m_displayList.append(cmd);
   }

   void CanvasRenderingContext2d::arcTo(float x1, float y1, float x2, float y2, float radius) {
      RecordedCommand cmd;
      cmd.type = CommandType::ArcTo;
      cmd.f0 = x1; cmd.f1 = y1; cmd.f2 = x2; cmd.f3 = y2; cmd.f4 = radius;
      m_displayList.append(cmd);
   }

   void CanvasRenderingContext2d::rect(float x, float y, float width, float height) {
      m_displayList.appendFloat4(CommandType::Rect, x, y, width, height);
   }

   void CanvasRenderingContext2d::roundRect(float x, float y, float width, float height, float radius) {
      RecordedCommand cmd;
      cmd.type = CommandType::RoundRect;
      cmd.f0 = x; cmd.f1 = y; cmd.f2 = width; cmd.f3 = height; cmd.f4 = radius;
      m_displayList.append(cmd);
   }

    void CanvasRenderingContext2d::arc(float x, float y, float radius,
                                       float startAngle, float endAngle, bool counterClockwise) {
       RecordedCommand cmd;
       cmd.type = CommandType::Arc;
       cmd.f0 = x; cmd.f1 = y; cmd.f2 = radius;
       cmd.f3 = startAngle; cmd.f4 = endAngle;
       cmd.i0 = counterClockwise ? 1 : 0;
       m_displayList.append(cmd);
    }

   void CanvasRenderingContext2d::ellipse(float x, float y, float rx, float ry, float rotation,
                                           float startAngle, float endAngle, bool counterClockwise) {
      RecordedCommand cmd;
      cmd.type = CommandType::Ellipse;
      cmd.f0 = x; cmd.f1 = y; cmd.f2 = rx; cmd.f3 = ry;
      cmd.f4 = rotation; cmd.f5 = startAngle;
      cmd.f6 = endAngle;
      cmd.i0 = counterClockwise ? 1 : 0;
      m_displayList.append(cmd);
   }

   void CanvasRenderingContext2d::fillRect(float x, float y, float width, float height) {
      bubbleDirty();
      RecordedCommand cmd;
      cmd.type = CommandType::FillRect;
      cmd.f0 = x; cmd.f1 = y; cmd.f2 = width; cmd.f3 = height;
      cmd.hasBounds = true;
      cmd.boundsX = x; cmd.boundsY = y; cmd.boundsW = width; cmd.boundsH = height;
      m_displayList.append(cmd);
      m_tiledCanvas.markDirty(x, y, width, height);
   }

   void CanvasRenderingContext2d::strokeRect(float x, float y, float width, float height) {
      bubbleDirty();
      RecordedCommand cmd;
      cmd.type = CommandType::StrokeRect;
      cmd.f0 = x; cmd.f1 = y; cmd.f2 = width; cmd.f3 = height;
      cmd.hasBounds = true;
      cmd.boundsX = x; cmd.boundsY = y; cmd.boundsW = width; cmd.boundsH = height;
      m_displayList.append(cmd);
      m_tiledCanvas.markDirty(x, y, width, height);
   }

   void CanvasRenderingContext2d::clearRect(float x, float y, float width, float height) {
      bubbleDirty();
      m_tiledCanvas.markDirty(x, y, width, height);
      m_displayList.appendFloat4(CommandType::ClearRect, x, y, width, height);
   }

   void CanvasRenderingContext2d::clear() {
      bubbleDirty();
      m_tiledCanvas.markAllDirty();
      m_displayList.appendSimple(CommandType::Clear);
   }

   // ============================================================================
   // getImageData / putImageData
   // ============================================================================

    ImageData* CanvasRenderingContext2d::getImageData(float sx, float sy, float sw, float sh) {
       int x = std::max(0, (int)sx);
       int y = std::max(0, (int)sy);
       int w = (int)sw;
       int h = (int)sh;
       if (x + w > m_width) w = m_width - x;
       if (y + h > m_height) h = m_height - y;
       if (w <= 0 || h <= 0) return new ImageData(nullptr, 0, 0);

       std::vector<uint8_t> raw(w * h * 4);

#ifndef __ANDROID__
        if (RenderThread* rt = RenderThread::instance()) {
            int fbo = renderFbo();
            if (fbo == 0) {
                // Render thread FBO not ready yet — render to main FBO and read
                GLint prevFbo = 0;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
                bindFbo();
                replayDisplayList();
                glReadPixels(x, m_height - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
                glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
            } else {
                // Read from render thread's FBO (has latest rendered content)
                GPUCommand cmd;
                cmd.type = GPUCommandType::ReadPixels;
                cmd.readCtx = this;
                cmd.readFbo = fbo;
                cmd.readX = x;
                cmd.readY = m_height - y - h;
                cmd.readW = w;
                cmd.readH = h;
                auto future = cmd.readPromise.get_future();
                rt->sendGPUCommand(std::move(cmd));
                raw = future.get();
            }
        } else
#endif
        {
           // Fallback: direct read from main thread's FBO (also used on Android)
           GLint prevFbo = 0;
           glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
           bindFbo();
           replayDisplayList();
           glReadPixels(x, m_height - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
           glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
       }

      // Convert GL RGBA (ABGR as uint32 on little-endian) → RGBA bytes with un-premultiply
      // glReadPixels with GL_RGBA returns R,G,B,A per pixel. NVG stores premultiplied alpha.
      std::vector<uint8_t> rgba(w * h * 4);
      for (int row = 0; row < h; row++) {
         // raw data is from bottom of FBO (row 0 = bottom of GL), but we need Y=0 = top
         // The glReadPixels call above already accounts for Y-flip, so row 0 in raw = top of canvas
         const uint8_t* srcRow = &raw[row * w * 4];
         uint8_t* dst = &rgba[row * w * 4];
         for (int col = 0; col < w; col++) {
            uint8_t r = srcRow[0], g = srcRow[1], b = srcRow[2], a = srcRow[3];
            // Un-premultiply
            if (a > 0 && a < 255) {
               r = (uint8_t)(((int)r * 255) / a);
               g = (uint8_t)(((int)g * 255) / a);
               b = (uint8_t)(((int)b * 255) / a);
            }
            dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = a;
            dst += 4;
            srcRow += 4;
         }
      }

      return new ImageData(rgba.data(), w, h);
   }

   void CanvasRenderingContext2d::putImageData(const ImageData& imageData, float dx, float dy) {
      putImageData(imageData, dx, dy, 0, 0, imageData.width(), imageData.height());
   }

   void CanvasRenderingContext2d::putImageData(const ImageData& imageData, float dx, float dy,
                                                float dirtyX, float dirtyY,
                                                float dirtyW, float dirtyH) {
      int srcW = imageData.width();
      int srcH = imageData.height();
      if (srcW == 0 || srcH == 0) return;

      int srcX = (int)dirtyX;
      int srcY = (int)dirtyY;
      int srcCopyW = (int)(dirtyW > 0 ? dirtyW : srcW - srcX);
      int srcCopyH = (int)(dirtyH > 0 ? dirtyH : srcH - srcY);

      if (srcX < 0) { srcCopyW += srcX; srcX = 0; }
      if (srcY < 0) { srcCopyH += srcY; srcY = 0; }
      if (srcX + srcCopyW > srcW) srcCopyW = srcW - srcX;
      if (srcY + srcCopyH > srcH) srcCopyH = srcH - srcY;
      if (srcCopyW <= 0 || srcCopyH <= 0) return;

      int dstX = (int)dx + srcX;
      int dstY = (int)dy + srcY;
      int dstW = srcCopyW;
      int dstH = srcCopyH;

      if (dstX < 0) { dstW += dstX; srcX -= dstX; dstX = 0; }
      if (dstY < 0) { dstH += dstY; srcY -= dstY; dstY = 0; }
      if (dstX + dstW > m_width) dstW = m_width - dstX;
      if (dstY + dstH > m_height) dstH = m_height - dstY;
      if (dstW <= 0 || dstH <= 0) return;

      // Premultiply incoming non-premultiplied data and upload to canvas texture
      std::vector<uint32_t> premul(dstW * dstH);
      const uint8_t* srcData = imageData.data();
      for (int row = 0; row < dstH; row++) {
         const uint8_t* srcRow = srcData + (srcY + row) * srcW * 4 + srcX * 4;
         for (int col = 0; col < dstW; col++) {
            uint8_t r = srcRow[0], g = srcRow[1], b = srcRow[2], a = srcRow[3];
            if (a < 255) {
               r = (uint8_t)(((int)r * a + 127) / 255);
               g = (uint8_t)(((int)g * a + 127) / 255);
               b = (uint8_t)(((int)b * a + 127) / 255);
            }
            premul[row * dstW + col] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                                        ((uint32_t)g << 8) | (uint32_t)b;
            srcRow += 4;
         }
      }

      // Convert from BGRA (premul uint32) to RGBA bytes for NVG image
      std::vector<uint8_t> rgbaData(dstW * dstH * 4);
      for (int row = 0; row < dstH; row++) {
         for (int col = 0; col < dstW; col++) {
            uint32_t px = premul[row * dstW + col];
            uint8_t b = px & 0xFF;
            uint8_t g = (px >> 8) & 0xFF;
            uint8_t r = (px >> 16) & 0xFF;
            uint8_t a = (px >> 24) & 0xFF;
            int off = (row * dstW + col) * 4;
            rgbaData[off]     = r;
            rgbaData[off + 1] = g;
            rgbaData[off + 2] = b;
            rgbaData[off + 3] = a;
         }
      }
      // Record for replay (replayed via NVG image draw to avoid GL FBO/texture cache issues)
      RecordedCommand cmd;
      cmd.type = CommandType::PutImageData;
      cmd.f0 = (float)dstX; cmd.f1 = (float)dstY; cmd.f2 = (float)dstW; cmd.f3 = (float)dstH;
      cmd.pixelData = std::move(rgbaData);
      m_displayList.append(cmd);

      m_tiledCanvas.markDirty((float)dstX, (float)dstY, (float)dstW, (float)dstH);
      bubbleDirty();
    }
}