#pragma once

namespace canvas {

class PlatformScreen {
public:
    virtual ~PlatformScreen() = default;

    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual float dpi() const = 0;
    virtual int angle() const = 0;
    virtual const char* orientationType() const = 0;
};

PlatformScreen* createPlatformScreen();

} // namespace canvas
