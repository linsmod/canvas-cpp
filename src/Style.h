#pragma once

#include <string>
#include <vector>

#include "Bindings.h"
#include "Color.h"

namespace canvas {

struct CSSValue {
    enum Type { Auto, Length, Percentage, ColorValue, Keyword, None, Inherit };

    Type type = Auto;
    float number = 0.0f;
    std::string unit;
    std::string keyword;
    std::string colorString;

    float toPx(float parentRef) const
    {
        switch (type) {
        case Length:     return number;
        case Percentage: return number / 100.0f * parentRef;
        default:         return 0.0f;
        }
    }

    bool isAuto() const { return type == Auto; }
    bool isNone() const { return type == None; }
    bool isValid() const { return type != None; }
    bool isPercent() const { return type == Percentage; }

    static CSSValue parse(const std::string& text);
};

struct TransformOp {
    enum Type { Translate, Rotate, Scale, SkewX, SkewY, Matrix };

    Type type = Translate;
    float args[6] = {};
    int argCount = 0;
    bool argIsPct[6] = {};
};

class StyleClass {
    DECLARE_QUICKJS_CLASS(StyleClass);

public:
    enum Display { DisplayNone, Block, Inline, InlineBlock, Flex };
    enum Position { Static, Relative, Absolute, Fixed };
    enum Overflow { OverflowVisible, OverflowHidden };
    enum BoxSizing { ContentBox, BorderBox };

    Display display = Block;
    Position position = Static;

    CSSValue width, height;
    CSSValue minWidth, minHeight;
    CSSValue maxWidth, maxHeight;

    CSSValue marginTop, marginRight, marginBottom, marginLeft;
    CSSValue paddingTop, paddingRight, paddingBottom, paddingLeft;
    CSSValue borderTop, borderRight, borderBottom, borderLeft;

    BoxSizing boxSizing = ContentBox;

    CSSValue left, top, right, bottom;

    Overflow overflowX = OverflowVisible;
    Overflow overflowY = OverflowVisible;

    Color backgroundColor;
    float opacity = 1.0f;
    bool visible = true;

    std::vector<TransformOp> transformOps;
    std::string transformString;

    bool isVisible() const
    {
        return display != DisplayNone && visible && opacity > 0.0f;
    }

    void setProperty(const std::string& name, const std::string& value);
    void setCSSText(const std::string& css);
    std::string getCSSText() const;
    std::string getPropertyValue(const std::string& name) const;

    static std::vector<TransformOp> parseTransform(const std::string& text);
    static std::string transformOpsToString(const std::vector<TransformOp>& ops);

    static std::string camelToKebab(const std::string& camel);
    static std::string kebabToCamel(const std::string& kebab);
};

using Style = StyleClass;
using Length = CSSValue;

} // namespace canvas

using canvas::CSSValue;
using canvas::Length;
using canvas::Style;
using canvas::StyleClass;
using canvas::TransformOp;
