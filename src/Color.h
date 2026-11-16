#pragma once

#include <vector>

struct Color {
    float r, g, b, a;

    Color(float rr = 0.0f, float gg = 0.0f, float bb = 0.0f, float aa = 1.0f)
        : r(rr), g(gg), b(bb), a(aa)
    {
    }
};

namespace canvas {

struct GradientStop {
    float offset;
    Color color;

    GradientStop() : offset(0.0f), color() {}
    GradientStop(float o, const Color& c) : offset(o), color(c) {}
};

struct GradientData {
    enum Type { NONE = 0, LINEAR = 1, RADIAL = 2 };

    Type type;
    float x0, y0, x1, y1;
    float r0, r1;
    std::vector<GradientStop> stops;

    GradientData()
        : type(NONE), x0(0.0f), y0(0.0f), x1(0.0f), y1(0.0f), r0(0.0f), r1(0.0f)
    {
    }
};

} // namespace canvas
