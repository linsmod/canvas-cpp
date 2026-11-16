#include "ColorParser.h"
#include "Log.h"

#include <regex>
#include <string>

Color ColorParser::parse(std::string const& color)
{
    Color resultColor;
    
    ColorMap::iterator search = m_cached.find(color);
    if (search != m_cached.end())
    {
        resultColor = search->second;
    }
    else
    {
        std::smatch m;

        // 1. rgba 浮点 alpha (已有)
        if (std::regex_match(color, m, std::regex(R"(rgba\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([0-9]+\.[0-9]+)\s*\))")))
        {
            resultColor = Color(std::stoi(m.str(1)) / 255.0f,
                                std::stoi(m.str(2)) / 255.0f,
                                std::stoi(m.str(3)) / 255.0f,
                                std::stof(m.str(4)));
            m_cached.insert(std::make_pair(color, resultColor));
        }
        // 2. rgba 整数 alpha (新增)
        else if (std::regex_match(color, m, std::regex(R"(rgba\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))")))
        {
            resultColor = Color(std::stoi(m.str(1)) / 255.0f,
                                std::stoi(m.str(2)) / 255.0f,
                                std::stoi(m.str(3)) / 255.0f,
                                std::stoi(m.str(4)) / 255.0f);
            m_cached.insert(std::make_pair(color, resultColor));
        }
        // 3. rgb
        else if (std::regex_match(color, m, std::regex(R"(rgb\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))")))
        {
            resultColor = Color(std::stoi(m.str(1)) / 255.0f,
                                std::stoi(m.str(2)) / 255.0f,
                                std::stoi(m.str(3)) / 255.0f, 1.0f);
            m_cached.insert(std::make_pair(color, resultColor));
        }
        // 4. #RRGGBBAA (新增)
        else if (std::regex_match(color, m, std::regex(R"(#([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2}))")))
        {
            resultColor = Color(hexStringToInt(m.str(1)) / 255.0f,
                                hexStringToInt(m.str(2)) / 255.0f,
                                hexStringToInt(m.str(3)) / 255.0f,
                                hexStringToInt(m.str(4)) / 255.0f);
            m_cached.insert(std::make_pair(color, resultColor));
        }
        // 5. #RGBA (新增)
        else if (std::regex_match(color, m, std::regex(R"(#([0-9a-fA-F])([0-9a-fA-F])([0-9a-fA-F])([0-9a-fA-F]))")))
        {
            std::string r = m.str(1), g = m.str(2), b = m.str(3), a = m.str(4);
            r += r; g += g; b += b; a += a;
            resultColor = Color(hexStringToInt(r) / 255.0f,
                                hexStringToInt(g) / 255.0f,
                                hexStringToInt(b) / 255.0f,
                                hexStringToInt(a) / 255.0f);
            m_cached.insert(std::make_pair(color, resultColor));
        }
        // 6. #RRGGBB (已有)
        else if (std::regex_match(color, m, std::regex(R"(#([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2}))")))
        {
            resultColor = Color(hexStringToInt(m.str(1)) / 255.0f,
                                hexStringToInt(m.str(2)) / 255.0f,
                                hexStringToInt(m.str(3)) / 255.0f, 1.0f);
            m_cached.insert(std::make_pair(color, resultColor));
        }
        // 7. #RGB (已有)
        else if (std::regex_match(color, m, std::regex(R"(#([0-9a-fA-F])([0-9a-fA-F])([0-9a-fA-F]))")))
        {
            std::string r = m.str(1), g = m.str(2), b = m.str(3);
            r += r; g += g; b += b;
            resultColor = Color(hexStringToInt(r) / 255.0f,
                                hexStringToInt(g) / 255.0f,
                                hexStringToInt(b) / 255.0f, 1.0f);
            m_cached.insert(std::make_pair(color, resultColor));
        }
        else
            LOGE("Invalid color: %s", color.c_str());
    }
    
    return resultColor;
}