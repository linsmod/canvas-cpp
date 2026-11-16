#include "PlatformScreen.h"
#include "util/OrientationBridge.h"

#include <SDL2/SDL.h>

namespace canvas {
namespace {

class SdlScreen : public PlatformScreen {
public:
    int width() const override
    {
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(0, &bounds) == 0) return bounds.w;
        return 0;
    }

    int height() const override
    {
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(0, &bounds) == 0) return bounds.h;
        return 0;
    }

    float dpi() const override
    {
        float horizontal = 0.0f, vertical = 0.0f, diagonal = 0.0f;
        if (SDL_GetDisplayDPI(0, &diagonal, &horizontal, &vertical) == 0 && horizontal > 0.0f) {
            return horizontal / 96.0f;
        }
        return 1.0f;
    }

    int angle() const override { return currentOrientationAngle(); }

    const char* orientationType() const override { return currentOrientationType(); }
};

} // namespace

PlatformScreen* createPlatformScreen()
{
    return new SdlScreen();
}

} // namespace canvas
