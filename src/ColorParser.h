#pragma once

#include <cstdlib>
#include <map>
#include <string>

#include "Color.h"

inline int hexStringToInt(const std::string& text)
{
    return static_cast<int>(std::strtol(text.c_str(), nullptr, 16));
}

class ColorParser {
public:
    Color parse(const std::string& color);

private:
    using ColorMap = std::map<std::string, Color>;
    ColorMap m_cached;
};
