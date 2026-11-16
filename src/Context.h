#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "NanoVG_GL.h"

#include "Bindings.h"
#include "Color.h"
#include "ColorParser.h"
#include "DisplayList.h"
#include "ImageData.h"
#include "RenderThread.h"
#include "State.h"
#include "Tiler.h"

namespace canvas {

class CanvasRenderingContext2d : public AnyClass {
    DECLARE_QUICKJS_CLASS(CanvasRenderingContext2d);

public:
    struct CachedGL {
        NVGcontext* nvgCtx;
        unsigned int fbo;
        unsigned int tex;
        unsigned int rb;
        int width;
        int height;
        std::map<std::string, int> fontCache;
    };

    explicit CanvasRenderingContext2d(int width, int height);
    ~CanvasRenderingContext2d() override;

    void setSize(int width, int height);
    uint32_t* getPixels();

    void swapDisplayList();
    void replayDisplayList(std::vector<RecordedCommand>* cmdBuf = nullptr);
    void replayOnThread(NVGcontext* renderNvgCtx, int targetFbo, int w, int h);

    void composite(uintptr_t target, int targetW, int targetH);
    void composite(uintptr_t target, int targetW, int targetH,
                   int offsetX, int offsetY, int clipW, int clipH);
    void composite(uintptr_t target, int targetW, int targetH,
                   int srcX, int srcY, int srcW, int srcH,
                   int dstX, int dstY, int dstW, int dstH);
    void copyImageTo(uintptr_t target);

    float lineWidth() const;
    void setLineWidth(float width);
    std::string lineCap() const;
    void setLineCap(const std::string& cap);
    std::string lineJoin() const;
    void setLineJoin(const std::string& join);

    std::string strokeStyle() const;
    void setStrokeStyle(const std::string& style);
    std::string fillStyle() const;
    void setFillStyle(const std::string& style);
    void setFillGradient(const GradientData& grad);
    void setStrokeGradient(const GradientData& grad);

    float globalAlpha() const;
    void setGlobalAlpha(float alpha);
    std::string globalCompositeOperation() const;
    void setGlobalCompositeOperation(const std::string& op);

    float shadowBlur() const;
    void setShadowBlur(float blur);
    std::string shadowColor() const;
    void setShadowColor(const std::string& color);
    float shadowOffsetX() const;
    void setShadowOffsetX(float x);
    float shadowOffsetY() const;
    void setShadowOffsetY(float y);

    bool imageSmoothingEnabled() const;
    void setImageSmoothingEnabled(bool enabled);
    std::string imageSmoothingQuality() const;
    void setImageSmoothingQuality(const std::string& quality);

    std::string font() const;
    void setFont(const std::string& fontStr);
    std::string textAlign() const;
    void setTextAlign(const std::string& align);
    std::string textBaseline() const;
    void setTextBaseline(const std::string& baseline);

    void fillText(const std::string& text, float x, float y);
    void strokeText(const std::string& text, float x, float y);
    void measureText(const std::string& text, double& outWidth,
                     double& outAscent, double& outDescent);

    void beginPath();
    void closePath();
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void quadraticCurveTo(float cpx, float cpy, float x, float y);
    void bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y,
                       float x, float y);
    void arc(float x, float y, float radius, float startAngle, float endAngle,
             bool counterClockwise = false);
    void arcTo(float x1, float y1, float x2, float y2, float radius);
    void ellipse(float x, float y, float rx, float ry, float rotation,
                 float startAngle, float endAngle, bool counterClockwise = false);
    void rect(float x, float y, float width, float height);
    void roundRect(float x, float y, float width, float height, float radius);
    void fill();
    void stroke();
    void clip();
    void fillRect(float x, float y, float width, float height);
    void strokeRect(float x, float y, float width, float height);
    void clearRect(float x, float y, float width, float height);
    void clear();

    void save();
    void restore();
    void translate(float x, float y);
    void scale(float x, float y);
    void rotate(float angle);
    void setTransform(float a, float b, float c, float d, float e, float f);
    void resetTransform();

    ImageData* getImageData(float sx, float sy, float sw, float sh);
    void putImageData(const ImageData& imageData, float dx, float dy);
    void putImageData(const ImageData& imageData, float dx, float dy,
                      float dirtyX, float dirtyY, float dirtyW, float dirtyH);
    void drawImage(ImageData* image, float x, float y, float width, float height);

    State& currentState() { return m_stateStack.back(); }
    const State& currentState() const { return m_stateStack.back(); }

    // 目前没有父 canvas 指针，保留钩子供后续实现脏区冒泡。
    void bubbleDirty() {}

    int renderFbo() const { return static_cast<int>(m_renderFbo); }
    int renderFboId() const { return static_cast<int>(m_renderFbo); }

private:
    static constexpr int kMaxCacheSize = 8;

    void initDefaultFont();
    int getFontHandle(const std::string& family, const std::string& weight);
    static int getFontHandleForContext(NVGcontext* nvgCtx,
                                       const std::string& family,
                                       const std::string& weight,
                                       std::map<std::string, int>& fontCache);
    static int mapCompositeOp(const std::string& op);
    static void parseCssFont(const std::string& fontStr, std::string& family,
                             std::string& weight, float& size);

    void applyTransformToNVG();
    void applyFillStyleToNVG();
    void applyStrokeStyleToNVG();
    void applyStateToNVG();
    void applyFillGradientToNVG(const GradientData& grad, float alpha);
    void applyStrokeGradientToNVG(const GradientData& grad, float alpha);
    void applyFontToNVG();

    void bindFbo() const;
    void createFbo(int width, int height);
    void deleteFbo();
    void blitToFbo(uintptr_t targetFbo, int targetW, int targetH,
                   int srcX, int srcY, int srcW, int srcH,
                   int dstX, int dstY, int dstW, int dstH);

    ColorParser m_parser;
    int m_width;
    int m_height;
    NVGcontext* m_nvgCtx = nullptr;
    NVGcontext* m_renderNvgCtx = nullptr;
    unsigned int m_fbo = 0;
    unsigned int m_tex = 0;
    unsigned int m_rb = 0;
    unsigned int m_renderFbo = 0;
    std::vector<uint32_t> m_readback;
    float m_currentXform[6] = {1, 0, 0, 1, 0, 0};
    bool m_clipActive = false;
    float m_clipX = 0.0f;
    float m_clipY = 0.0f;
    float m_clipW = 0.0f;
    float m_clipH = 0.0f;
    std::vector<State> m_stateStack;
    DisplayList m_displayList;
    Tiler m_tiledCanvas;
    std::map<std::string, int> m_fontHandleCache;
    std::map<std::string, int> m_renderFontHandleCache;

    static std::map<std::string, std::string> s_fontPathCache;
    static std::vector<CachedGL> s_glCache;
};

} // namespace canvas
