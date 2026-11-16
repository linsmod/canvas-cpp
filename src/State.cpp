#include "State.h"

namespace canvas {

State::State()
    : lineWidth(1.0f)
    , lineCap("butt")
    , lineJoin("miter")
    , strokeStyleString("black")
    , fillStyleString("black")
    , strokeStyle(0.0f, 0.0f, 0.0f, 1.0f)
    , fillStyle(0.0f, 0.0f, 0.0f, 1.0f)
    , globalAlpha(1.0f)
    , hasFillGradient(false)
    , fillGradientData()
    , hasStrokeGradient(false)
    , strokeGradientData()
    , font("10px sans-serif")
    , textAlign("left")
    , textBaseline("alphabetic")
    , globalCompositeOperation("source-over")
    , shadowBlur(0.0f)
    , shadowOffsetX(0.0f)
    , shadowOffsetY(0.0f)
    , shadowColor("transparent")
    , imageSmoothingEnabled(true)
    , imageSmoothingQuality("low")
    , cachedFontString()
    , cachedFamily("sans-serif")
    , cachedWeight("normal")
    , cachedSize(12.0f)
{
    transform[0] = 1.0f; transform[1] = 0.0f;
    transform[2] = 0.0f; transform[3] = 1.0f;
    transform[4] = 0.0f; transform[5] = 0.0f;
}

int State::nvgLineCap() const
{
    if (lineCap == "round") return kNvgRound;
    if (lineCap == "square") return kNvgSquare;
    return kNvgButt;
}

int State::nvgLineJoin() const
{
    if (lineJoin == "round") return kNvgRound;
    if (lineJoin == "bevel") return kNvgBevel;
    return kNvgMiter;
}

int State::nvgTextAlign() const
{
    int align = 0;
    if (textAlign == "center") align |= kNvgAlignCenter;
    else if (textAlign == "right" || textAlign == "end") align |= kNvgAlignRight;
    else align |= kNvgAlignLeft;

    if (textBaseline == "top" || textBaseline == "hanging") align |= kNvgAlignTop;
    else if (textBaseline == "middle") align |= kNvgAlignMiddle;
    else if (textBaseline == "ideographic" || textBaseline == "bottom") align |= kNvgAlignBottom;
    else align |= kNvgAlignBaseline;

    return align;
}

} // namespace canvas
