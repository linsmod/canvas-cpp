#pragma once

#include <string>

#include "Color.h"

namespace canvas {

// 与 nanovg.h 中的 NVGlineCap / NVGalign 数值保持一致；
// 类型层不依赖 NanoVG 头文件，因此在此处镜像这几个常量。
enum NvgCapConstant {
    kNvgButt = 0,
    kNvgRound = 1,
    kNvgSquare = 2,
    kNvgMiter = 3,
    kNvgBevel = 4
};

enum NvgAlignConstant {
    kNvgAlignLeft     = 1 << 0,
    kNvgAlignCenter   = 1 << 1,
    kNvgAlignRight    = 1 << 2,
    kNvgAlignTop      = 1 << 3,
    kNvgAlignMiddle   = 1 << 4,
    kNvgAlignBottom   = 1 << 5,
    kNvgAlignBaseline = 1 << 6
};

class State {
public:
    State();

    float lineWidth;
    std::string lineCap;
    std::string lineJoin;

    std::string strokeStyleString;
    std::string fillStyleString;
    Color strokeStyle;
    Color fillStyle;

    float globalAlpha;

    bool hasFillGradient;
    GradientData fillGradientData;
    bool hasStrokeGradient;
    GradientData strokeGradientData;

    std::string font;
    std::string textAlign;
    std::string textBaseline;

    std::string globalCompositeOperation;

    float shadowBlur;
    float shadowOffsetX;
    float shadowOffsetY;
    std::string shadowColor;

    bool imageSmoothingEnabled;
    std::string imageSmoothingQuality;

    float transform[6];

    mutable std::string cachedFontString;
    mutable std::string cachedFamily;
    mutable std::string cachedWeight;
    mutable float cachedSize;

    int nvgLineCap() const;
    int nvgLineJoin() const;
    int nvgTextAlign() const;
};

} // namespace canvas
