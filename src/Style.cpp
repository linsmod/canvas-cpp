#include "Style.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace canvas {

IMPL_QUICKJS_CLASS(StyleClass);

namespace {

std::string trim(const std::string& s)
{
    std::size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    std::size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string> split(const std::string& s, char separator)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t pos = s.find(separator, start);
        if (pos == std::string::npos) {
            result.push_back(s.substr(start));
            break;
        }
        result.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return result;
}

bool startsWith(const std::string& s, const char* prefix)
{
    return s.compare(0, std::string::traits_type::length(prefix), prefix) == 0;
}

Color parseColor(const std::string& s)
{
    if (s.size() == 7 && s[0] == '#') {
        unsigned int rgb = 0;
        if (std::sscanf(s.c_str() + 1, "%6x", &rgb) == 1) {
            return Color(((rgb >> 16) & 0xFF) / 255.0f,
                         ((rgb >> 8) & 0xFF) / 255.0f,
                         (rgb & 0xFF) / 255.0f, 1.0f);
        }
    }
    if (s.size() == 4 && s[0] == '#') {
        unsigned int rgb = 0;
        if (std::sscanf(s.c_str() + 1, "%3x", &rgb) == 1) {
            return Color((((rgb >> 8) & 0xF) * 17) / 255.0f,
                         (((rgb >> 4) & 0xF) * 17) / 255.0f,
                         ((rgb & 0xF) * 17) / 255.0f, 1.0f);
        }
    }
    return Color(0.0f, 0.0f, 0.0f, 1.0f);
}

} // namespace

CSSValue CSSValue::parse(const std::string& text)
{
    CSSValue value;
    const std::string s = trim(text);
    if (s.empty()) {
        value.type = None;
        return value;
    }
    if (s == "auto") {
        value.type = Auto;
        return value;
    }
    if (s == "inherit") {
        value.type = Inherit;
        value.keyword = s;
        return value;
    }
    if (s == "none") {
        value.type = None;
        value.keyword = s;
        return value;
    }
    if (s[0] == '#' || startsWith(s, "rgb(") || startsWith(s, "rgba(")) {
        value.type = ColorValue;
        value.colorString = s;
        return value;
    }

    std::size_t pos = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    bool hasDigit = false;
    for (; pos < s.size(); ++pos) {
        if (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.') {
            hasDigit = true;
        } else {
            break;
        }
    }
    if (hasDigit) {
        value.number = std::strtof(s.substr(0, pos).c_str(), nullptr);
        value.unit = trim(s.substr(pos));
        if (value.unit == "%") {
            value.type = Percentage;
        } else {
            value.type = Length;
            if (value.unit == "em" || value.unit == "rem") value.number *= 16.0f;
        }
        return value;
    }

    value.type = Keyword;
    value.keyword = s;
    return value;
}

std::string StyleClass::camelToKebab(const std::string& camel)
{
    std::string result;
    for (char c : camel) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            if (!result.empty()) result += '-';
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            result += c;
        }
    }
    return result;
}

std::string StyleClass::kebabToCamel(const std::string& kebab)
{
    std::string result;
    bool upperNext = false;
    for (char c : kebab) {
        if (c == '-') {
            upperNext = true;
        } else if (upperNext) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            upperNext = false;
        } else {
            result += c;
        }
    }
    return result;
}

void StyleClass::setProperty(const std::string& rawName, const std::string& rawValue)
{
    const std::string name = kebabToCamel(trim(rawName));
    const std::string value = trim(rawValue);
    const std::string enumValue = kebabToCamel(value);
    if (name.empty()) return;

    if (name == "cssText") {
        setCSSText(value);
        return;
    }
    if (name == "display") {
        if (enumValue == "none") display = DisplayNone;
        else if (enumValue == "inline") display = Inline;
        else if (enumValue == "inlineBlock") display = InlineBlock;
        else if (enumValue == "flex") display = Flex;
        else display = Block;
        return;
    }
    if (name == "position") {
        if (enumValue == "relative") position = Relative;
        else if (enumValue == "absolute") position = Absolute;
        else if (enumValue == "fixed") position = Fixed;
        else position = Static;
        return;
    }
    if (name == "boxSizing") {
        boxSizing = (enumValue == "borderBox") ? BorderBox : ContentBox;
        return;
    }
    if (name == "overflowX" || name == "overflowY") {
        Overflow overflow = (value == "hidden") ? OverflowHidden : OverflowVisible;
        if (name == "overflowX") overflowX = overflow;
        else overflowY = overflow;
        return;
    }
    if (name == "opacity") {
        CSSValue parsed = CSSValue::parse(value);
        opacity = parsed.type == CSSValue::Length ? parsed.number : 1.0f;
        return;
    }
    if (name == "visibility") {
        visible = (value != "hidden" && value != "collapse");
        return;
    }
    if (name == "backgroundColor") {
        backgroundColor = parseColor(value);
        return;
    }
    if (name == "transform") {
        transformString = value;
        transformOps = parseTransform(value);
        return;
    }

    CSSValue StyleClass::* target = nullptr;
    if (name == "width") target = &StyleClass::width;
    else if (name == "height") target = &StyleClass::height;
    else if (name == "minWidth") target = &StyleClass::minWidth;
    else if (name == "minHeight") target = &StyleClass::minHeight;
    else if (name == "maxWidth") target = &StyleClass::maxWidth;
    else if (name == "maxHeight") target = &StyleClass::maxHeight;
    else if (name == "marginTop") target = &StyleClass::marginTop;
    else if (name == "marginRight") target = &StyleClass::marginRight;
    else if (name == "marginBottom") target = &StyleClass::marginBottom;
    else if (name == "marginLeft") target = &StyleClass::marginLeft;
    else if (name == "paddingTop") target = &StyleClass::paddingTop;
    else if (name == "paddingRight") target = &StyleClass::paddingRight;
    else if (name == "paddingBottom") target = &StyleClass::paddingBottom;
    else if (name == "paddingLeft") target = &StyleClass::paddingLeft;
    else if (name == "borderTop") target = &StyleClass::borderTop;
    else if (name == "borderRight") target = &StyleClass::borderRight;
    else if (name == "borderBottom") target = &StyleClass::borderBottom;
    else if (name == "borderLeft") target = &StyleClass::borderLeft;
    else if (name == "left") target = &StyleClass::left;
    else if (name == "top") target = &StyleClass::top;
    else if (name == "right") target = &StyleClass::right;
    else if (name == "bottom") target = &StyleClass::bottom;

    if (target) this->*target = CSSValue::parse(value);
}

void StyleClass::setCSSText(const std::string& css)
{
    for (const std::string& declaration : split(css, ';')) {
        std::size_t colon = declaration.find(':');
        if (colon == std::string::npos) continue;
        setProperty(declaration.substr(0, colon), declaration.substr(colon + 1));
    }
}

std::string StyleClass::getPropertyValue(const std::string& rawName) const
{
    const std::string name = kebabToCamel(trim(rawName));
    if (name == "display") {
        switch (display) {
        case DisplayNone: return "none";
        case Inline: return "inline";
        case InlineBlock: return "inline-block";
        case Flex: return "flex";
        default: return "block";
        }
    }
    if (name == "position") {
        switch (position) {
        case Relative: return "relative";
        case Absolute: return "absolute";
        case Fixed: return "fixed";
        default: return "static";
        }
    }
    if (name == "boxSizing") return boxSizing == BorderBox ? "border-box" : "content-box";
    if (name == "opacity") return std::to_string(opacity);
    if (name == "visibility") return visible ? "visible" : "hidden";
    if (name == "backgroundColor") return "rgb(0,0,0)";
    if (name == "transform") return transformString;

    const CSSValue StyleClass::* target = nullptr;
    if (name == "width") target = &StyleClass::width;
    else if (name == "height") target = &StyleClass::height;
    else if (name == "minWidth") target = &StyleClass::minWidth;
    else if (name == "minHeight") target = &StyleClass::minHeight;
    else if (name == "maxWidth") target = &StyleClass::maxWidth;
    else if (name == "maxHeight") target = &StyleClass::maxHeight;
    else if (name == "marginTop") target = &StyleClass::marginTop;
    else if (name == "marginRight") target = &StyleClass::marginRight;
    else if (name == "marginBottom") target = &StyleClass::marginBottom;
    else if (name == "marginLeft") target = &StyleClass::marginLeft;
    else if (name == "paddingTop") target = &StyleClass::paddingTop;
    else if (name == "paddingRight") target = &StyleClass::paddingRight;
    else if (name == "paddingBottom") target = &StyleClass::paddingBottom;
    else if (name == "paddingLeft") target = &StyleClass::paddingLeft;
    else if (name == "borderTop") target = &StyleClass::borderTop;
    else if (name == "borderRight") target = &StyleClass::borderRight;
    else if (name == "borderBottom") target = &StyleClass::borderBottom;
    else if (name == "borderLeft") target = &StyleClass::borderLeft;
    else if (name == "left") target = &StyleClass::left;
    else if (name == "top") target = &StyleClass::top;
    else if (name == "right") target = &StyleClass::right;
    else if (name == "bottom") target = &StyleClass::bottom;
    if (!target) return std::string();

    const CSSValue& v = this->*target;
    std::ostringstream out;
    switch (v.type) {
    case CSSValue::Auto:    return "auto";
    case CSSValue::None:    return "none";
    case CSSValue::Inherit: return "inherit";
    case CSSValue::Keyword: return v.keyword;
    case CSSValue::ColorValue: return v.colorString;
    case CSSValue::Percentage:
        out << v.number << '%';
        return out.str();
    case CSSValue::Length:
        out << v.number << (v.unit.empty() ? "px" : v.unit);
        return out.str();
    }
    return std::string();
}

std::string StyleClass::getCSSText() const
{
    static const char* kProperties[] = {
        "display", "position", "width", "height", "min-width", "min-height",
        "max-width", "max-height", "margin-top", "margin-right", "margin-bottom",
        "margin-left", "padding-top", "padding-right", "padding-bottom", "padding-left",
        "border-top", "border-right", "border-bottom", "border-left",
        "left", "top", "right", "bottom", "box-sizing", "opacity", "visibility", "transform"
    };
    std::string result;
    for (const char* property : kProperties) {
        std::string value = getPropertyValue(property);
        if (value.empty()) continue;
        if (!result.empty()) result += "; ";
        result += property;
        result += ": ";
        result += value;
    }
    return result;
}

std::vector<TransformOp> StyleClass::parseTransform(const std::string& text)
{
    std::vector<TransformOp> ops;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t open = text.find('(', pos);
        if (open == std::string::npos) break;
        const std::size_t close = text.find(')', open + 1);
        if (close == std::string::npos) break;

        const std::string function = trim(text.substr(pos, open - pos));
        TransformOp op;
        if (function == "translate") op.type = TransformOp::Translate;
        else if (function == "rotate") op.type = TransformOp::Rotate;
        else if (function == "scale") op.type = TransformOp::Scale;
        else if (function == "skewX") op.type = TransformOp::SkewX;
        else if (function == "skewY") op.type = TransformOp::SkewY;
        else if (function == "matrix") op.type = TransformOp::Matrix;
        else {
            pos = close + 1;
            continue;
        }

        for (const std::string& item : split(text.substr(open + 1, close - open - 1), ',')) {
            if (op.argCount >= 6) break;
            std::string arg = trim(item);
            if (!arg.empty() && arg.back() == '%') {
                op.argIsPct[op.argCount] = true;
                arg.pop_back();
            }
            op.args[op.argCount] = std::strtof(arg.c_str(), nullptr);
            ++op.argCount;
        }
        ops.push_back(op);
        pos = close + 1;
    }
    return ops;
}

std::string StyleClass::transformOpsToString(const std::vector<TransformOp>& ops)
{
    static const char* kNames[] = {
        "translate", "rotate", "scale", "skewX", "skewY", "matrix"
    };
    std::string result;
    for (const TransformOp& op : ops) {
        if (op.type < TransformOp::Translate || op.type > TransformOp::Matrix) continue;
        if (op.argCount <= 0) continue;
        if (!result.empty()) result += ' ';
        result += kNames[op.type];
        result += '(';
        for (int i = 0; i < op.argCount; ++i) {
            if (i > 0) result += ", ";
            std::ostringstream out;
            out << op.args[i];
            result += out.str();
            if (op.argIsPct[i]) result += '%';
            else if (op.type == TransformOp::Rotate ||
                     op.type == TransformOp::SkewX ||
                     op.type == TransformOp::SkewY) result += "deg";
            else if (op.type == TransformOp::Translate) result += "px";
        }
        result += ')';
    }
    return result;
}

} // namespace canvas
