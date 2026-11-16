#pragma once

#include <string>

#include "Bindings.h"

namespace canvas {

class Orientation : public AnyClass {
    DECLARE_QUICKJS_CLASS(Orientation);

public:
    Orientation();

    std::string type;
    int angle = 0;

    void refresh();
};

} // namespace canvas
