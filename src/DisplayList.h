#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Color.h"

namespace canvas {

enum class CommandType {
    BeginPath,
    ClosePath,
    MoveTo,
    LineTo,
    QuadraticCurveTo,
    BezierCurveTo,
    Arc,
    ArcTo,
    Ellipse,
    Rect,
    RoundRect,
    Clip,
    Save,
    Restore,
    Translate,
    Scale,
    Rotate,
    SetTransform,
    ResetTransform,
    SetFillStyle,
    SetStrokeStyle,
    SetFillGradient,
    SetStrokeGradient,
    SetLineWidth,
    SetLineCap,
    SetLineJoin,
    SetGlobalAlpha,
    SetGlobalCompositeOperation,
    SetShadowBlur,
    SetShadowColor,
    SetShadowOffsetX,
    SetShadowOffsetY,
    SetFont,
    SetTextAlign,
    SetTextBaseline,
    Fill,
    Stroke,
    FillRect,
    StrokeRect,
    ClearRect,
    Clear,
    FillText,
    StrokeText,
    DrawImage,
    PutImageData,
    CreateFBO,
    DeleteFBO,
    ReadPixels,
    CleanupCanvas
};

struct RecordedCommand {
    CommandType type = CommandType::Clear;

    float f0 = 0.0f, f1 = 0.0f, f2 = 0.0f;
    float f3 = 0.0f, f4 = 0.0f, f5 = 0.0f, f6 = 0.0f;
    int i0 = 0;
    std::string s0;
    Color color0;
    GradientData gradient;

    std::vector<std::uint8_t> pixelData;
    int imgWidth = 0;
    int imgHeight = 0;

    bool hasBounds = false;
    float boundsX = 0.0f, boundsY = 0.0f;
    float boundsW = 0.0f, boundsH = 0.0f;
};

// 双缓冲：主线程只写 back，渲染线程只读 front。
// swap() 在录制完成、提交帧之前由主线程调用一次；调用方必须保证
// 上一帧已由 waitForFrame / GL fence 完成，否则旧 front 仍在被读取。
class DisplayList {
public:
    void append(const RecordedCommand& cmd) { m_back.push_back(cmd); }
    void append(RecordedCommand&& cmd) { m_back.push_back(std::move(cmd)); }

    void appendSimple(CommandType type)
    {
        RecordedCommand cmd;
        cmd.type = type;
        append(cmd);
    }

    void appendFloat(CommandType type, float a)
    {
        RecordedCommand cmd;
        cmd.type = type;
        cmd.f0 = a;
        append(cmd);
    }

    void appendFloat2(CommandType type, float a, float b)
    {
        RecordedCommand cmd;
        cmd.type = type;
        cmd.f0 = a;
        cmd.f1 = b;
        append(cmd);
    }

    void appendFloat3(CommandType type, float a, float b, float c)
    {
        RecordedCommand cmd;
        cmd.type = type;
        cmd.f0 = a;
        cmd.f1 = b;
        cmd.f2 = c;
        append(cmd);
    }

    void appendFloat4(CommandType type, float a, float b, float c, float d)
    {
        RecordedCommand cmd;
        cmd.type = type;
        cmd.f0 = a;
        cmd.f1 = b;
        cmd.f2 = c;
        cmd.f3 = d;
        append(cmd);
    }

    void appendFloat6(CommandType type, float a, float b, float c, float d, float e, float f)
    {
        RecordedCommand cmd;
        cmd.type = type;
        cmd.f0 = a;
        cmd.f1 = b;
        cmd.f2 = c;
        cmd.f3 = d;
        cmd.f4 = e;
        cmd.f5 = f;
        append(cmd);
    }

    std::vector<RecordedCommand>* front() { return &m_front; }
    const std::vector<RecordedCommand>* front() const { return &m_front; }
    std::vector<RecordedCommand>* back() { return &m_back; }
    const std::vector<RecordedCommand>* back() const { return &m_back; }

    void swap()
    {
        m_front.swap(m_back);
        m_back.clear();
    }

    void clear() { m_back.clear(); }

    std::size_t size() const { return m_front.size(); }
    bool empty() const { return m_front.empty(); }

private:
    std::vector<RecordedCommand> m_front;
    std::vector<RecordedCommand> m_back;
};

} // namespace canvas
