#pragma once

#include <memory>
#include <vector>

#include "Layout.h"
#include "Style.h"

struct JSContext;

namespace canvas {

class ElementClass;
class CanvasClass;

class RenderNode {
public:
    struct Rect {
        float x = 0.0f, y = 0.0f;
        float width = 0.0f, height = 0.0f;
        float paddingLeft = 0.0f, paddingRight = 0.0f;
        float paddingTop = 0.0f, paddingBottom = 0.0f;
        float borderLeft = 0.0f, borderRight = 0.0f;
        float borderTop = 0.0f, borderBottom = 0.0f;
        bool dirty = true;
    };

    Style style;
    Rect computed;
    float paddingLeft = 0.0f, paddingRight = 0.0f;
    float paddingTop = 0.0f, paddingBottom = 0.0f;
    float borderLeft = 0.0f, borderRight = 0.0f;
    float borderTop = 0.0f, borderBottom = 0.0f;
    bool dirty = true;

    std::vector<std::unique_ptr<RenderNode>> children;
    RenderNode* parent = nullptr;
    ElementClass* domNode = nullptr;

    RenderNode();
    virtual ~RenderNode();

    void setStyle(const Style& s) { style = s; }
    bool isVisible() const { return style.isVisible(); }

    void layoutRoot(float viewportW, float viewportH);
    virtual void layout(float parentContentWidth, float parentContentHeight);

    virtual void getBoundingRect(float& x, float& y, float& w, float& h) const;
    virtual ElementClass* hitTest(float px, float py) const;
    virtual void paint(void* target, int targetW, int targetH, JSContext* ctx);

    bool hasDirty(int flags) const { return (m_dirtyFlags & flags) != 0; }
    void clearDirty(int flags) { m_dirtyFlags &= ~flags; }
    void bubbleDirty(int flags = CONTENT);

private:
    int m_dirtyFlags = kDirtyAll;
};

class RenderCanvas : public RenderNode {
public:
    CanvasClass* canvas = nullptr;

    RenderCanvas(CanvasClass* canvasElement, ElementClass* domElement);

    void layout(float parentW, float parentH) override;
    void getBoundingRect(float& x, float& y, float& w, float& h) const override;
    ElementClass* hitTest(float px, float py) const override;
    void paint(void* target, int targetW, int targetH, JSContext* ctx) override;
};

std::unique_ptr<RenderNode> buildRenderTree(JSContext* ctx, ElementClass* root);
RenderNode* findRenderNode(RenderNode* root, ElementClass* el);

} // namespace canvas

using canvas::RenderCanvas;
using canvas::RenderNode;
using canvas::buildRenderTree;
using canvas::findRenderNode;
