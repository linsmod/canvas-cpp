#include "RenderNode.h"
#include "Context.h"
#include "DOMClasses.h"
#include "Log.h"
#include "quickjs.h"
#include <cstring>
#include <cmath>

namespace canvas {

RenderNode::RenderNode() = default;

RenderNode::~RenderNode() = default;

void RenderNode::bubbleDirty(int flags)
{
    m_dirtyFlags |= flags;
    if (parent) parent->bubbleDirty(flags);
}

RenderCanvas::RenderCanvas(canvas::CanvasClass* canvasElement, canvas::ElementClass* domElement)
    : canvas(canvasElement)
{
    domNode = domElement;
}

// ========== 辅助函数 ==========
static float resolveLength(const CSSValue& length, float ref, bool treatAutoAsZero = false) {
    if (length.isAuto()) return treatAutoAsZero ? 0.0f : -1.0f;   // -1 表示 auto
    return length.toPx(ref);
}

static float resolveMargin(const CSSValue& length, float ref) {
    return length.isAuto() ? 0.0f : length.toPx(ref);
}

// 判断一个节点的内容高度是否已被确定（非 auto 或已经布局计算得出）
static bool isHeightDefined(const RenderNode* node) {
    if (!node) return false;
    return !node->style.height.isAuto() || node->computed.height > 0;
}

// 应用 Transform 中的平移（仅影响位置，不影响尺寸）
static void applyTranslateOnly(float& x, float& y, float elW, float elH,
                               const std::vector<TransformOp>& ops) {
    for (const auto& op : ops) {
        if (op.type == TransformOp::Translate) {
            float tx = op.argIsPct[0] ? op.args[0] / 100.0f * elW : op.args[0];
            float ty = (op.argCount >= 2)
                ? (op.argIsPct[1] ? op.args[1] / 100.0f * elH : op.args[1])
                : 0;
            x += tx;
            y += ty;
        }
    }
}

// 递归累加从根到节点的偏移 + transform
static void accumulateTransform(const RenderNode* node, float& x, float& y) {
    if (!node) return;
    accumulateTransform(node->parent, x, y);
    x += node->computed.x;
    y += node->computed.y;
    float refW = node->computed.width;
    float refH = node->computed.height;
    if (const RenderCanvas* cn = dynamic_cast<const RenderCanvas*>(node)) {
        if (cn->canvas && cn->canvas->width > 0 && cn->canvas->height > 0) {
            float cssW = node->computed.width;
            float cssH = node->computed.height;
            refW = (cssW > 0) ? std::min(cssW, (float)cn->canvas->width) : (float)cn->canvas->width;
            refH = (cssH > 0) ? std::min(cssH, (float)cn->canvas->height) : (float)cn->canvas->height;
        }
    }
    applyTranslateOnly(x, y, refW, refH, node->style.transformOps);
}

// ========== 静态布局函数声明（内部使用） ==========
static void layoutAbsoluteChild(RenderNode* child,
                                float cbLeft, float cbTop,
                                float cbWidth, float cbHeight,
                                float parentContentWidth, float parentContentHeight);
static void layoutBlockChild(RenderNode* child,
                             float& x, float& y,
                             float availableWidth,
                             float parentContentWidth, float parentContentHeight,
                             float& prevMarginBottom);
static void layoutChildren(RenderNode* node,
                           float parentContentWidth, float parentContentHeight);

// ========== 根节点专用布局（静态入口） ==========
void layoutRoot(RenderNode* root, float viewportW, float viewportH) {
    if (!root || !root->isVisible()) return;

    // 根节点位置和初始尺寸（视口区域）
    root->computed.x = 0;
    root->computed.y = 0;
    root->computed.width = viewportW;
    root->computed.height = viewportH;

    // 样式中的宽高覆盖视口
    if (!root->style.width.isAuto())
        root->computed.width = root->style.width.toPx(viewportW);
    if (!root->style.height.isAuto())
        root->computed.height = root->style.height.toPx(viewportH);

    // 布局子节点（传入内容区尺寸）
    float contentW = std::max(0.0f, root->computed.width - root->paddingLeft - root->paddingRight);
    float contentH = std::max(0.0f, root->computed.height - root->paddingTop - root->paddingBottom);
    layoutChildren(root, contentW, contentH);
}

// ========== 普通节点布局入口（静态） ==========
void layoutNode(RenderNode* node, float parentContentWidth, float parentContentHeight) {
    if (!node || !node->isVisible()) return;
    // 普通节点：自身位置尺寸已由父节点设置，直接布局子节点
    float contentW = std::max(0.0f, node->computed.width - node->paddingLeft - node->paddingRight);
    float contentH = std::max(0.0f, node->computed.height - node->paddingTop - node->paddingBottom);
    layoutChildren(node, contentW, contentH);
}

// ========== 核心子节点布局（静态） ==========
static void layoutChildren(RenderNode* node,
                           float parentContentWidth, float parentContentHeight) {
    node->computed.dirty = false;

    // 包含块信息（绝对定位使用 padding box）
    float cbLeft = node->computed.x + node->borderLeft;
    float cbTop  = node->computed.y + node->borderTop;
    float cbWidth  = node->computed.width - node->borderLeft - node->borderRight;
    float cbHeight = node->computed.height - node->borderTop - node->borderBottom;

    // 普通流起始点（content box 左上角）
    float x = node->paddingLeft;
    float y = node->paddingTop;
    float availableW = std::max(0.0f, node->computed.width - node->paddingLeft - node->paddingRight);

    float prevMarginBottom = 0;   // 用于兄弟边距折叠

    for (auto& child : node->children) {
        if (!child->isVisible()) continue;

        // 绝对 / 固定定位子节点
        if (child->style.position == StyleClass::Absolute ||
            child->style.position == StyleClass::Fixed) {
            layoutAbsoluteChild(child.get(),
                                cbLeft, cbTop, cbWidth, cbHeight,
                                parentContentWidth, parentContentHeight);
            continue;
        }

        // 普通流块级子节点
        layoutBlockChild(child.get(),
                         x, y, availableW,
                         parentContentWidth, parentContentHeight,
                         prevMarginBottom);
    }

    // 若当前节点高度为 auto，由子节点撑开
    if (node->style.height.isAuto() && !node->children.empty()) {
        node->computed.height = y + node->paddingBottom;
    }
}

static void layoutAbsoluteChild(RenderNode* child,
                                float cbLeft, float cbTop,
                                float cbWidth, float cbHeight,
                                float /*parentContentWidth*/, float parentContentHeight) {
    const auto& s = child->style;
    float boxW = cbWidth;
    float boxH = cbHeight;

    // 1. 计算宽度
    float cw;
    bool widthAuto = s.width.isAuto();
    if (!widthAuto) {
        cw = s.width.toPx(boxW);
    } else {
        // auto 宽度：如果 left/right 都指定，则宽度 = 包含块宽度 - left - right
        if (!s.left.isAuto() && !s.right.isAuto()) {
            cw = boxW - s.left.toPx(boxW) - s.right.toPx(boxW);
        } else {
            cw = 0;   // 未实现 shrink-to-fit，暂设为 0（后续可由内容撑开？规范中 auto 宽度为 shrink-to-fit）
        }
    }

    // 2. 计算高度
    float ch;
    bool heightAuto = s.height.isAuto();
    if (!heightAuto) {
        if (s.height.isPercent() && !isHeightDefined(child->parent)) {
            heightAuto = true;
            ch = 0;
        } else {
            ch = s.height.toPx(boxH);
        }
    } else {
        ch = 0;
    }

    // 3. 计算 left / top 偏移
    float cx = cbLeft + child->paddingLeft;
    float cy = cbTop + child->paddingTop;

    if (!s.left.isAuto())
        cx = cbLeft + s.left.toPx(boxW);
    else if (!s.right.isAuto())
        cx = cbLeft + boxW - s.right.toPx(boxW) - cw;

    if (!s.top.isAuto())
        cy = cbTop + s.top.toPx(boxH);
    else if (!s.bottom.isAuto())
        cy = cbTop + boxH - s.bottom.toPx(boxH) - ch;

    // 4. 临时设置位置和尺寸
    child->computed.x = cx;
    child->computed.y = cy;
    child->computed.width = cw;
    child->computed.height = ch;

    // 5. 布局子节点（绝对定位内部仍可能存在普通流）
    layoutChildren(child, child->computed.width, child->computed.height);

    // 6. 若高度为 auto，根据子节点重新计算高度
    if (heightAuto) {
        float contentH = 0;
        for (auto& gc : child->children) {
            if (!gc->isVisible()) continue;
            float gcBottom = gc->computed.y + gc->computed.height
                           + resolveMargin(gc->style.marginBottom, gc->computed.width);
            contentH = std::max(contentH, gcBottom);
        }
        float totalHeight = contentH + child->paddingTop + child->paddingBottom
                          + child->borderTop + child->borderBottom;
        child->computed.height = totalHeight;

        // 如果同时指定了 top 和 bottom，根据 bottom 约束调整 y 位置
        if (!s.top.isAuto() && !s.bottom.isAuto()) {
            child->computed.y = cbTop + boxH - s.bottom.toPx(boxH) - child->computed.height;
        }
    }

    // 7. 若宽度为 auto 且 left/right 都指定，重新计算宽度（保持约束）
    if (widthAuto && !s.left.isAuto() && !s.right.isAuto()) {
        float newCw = boxW - s.left.toPx(boxW) - s.right.toPx(boxW);
        child->computed.width = std::max(0.0f, newCw);
    }
}

static void layoutBlockChild(RenderNode* child,
                             float& x, float& y,
                             float availableWidth,
                             float parentContentWidth, float parentContentHeight,
                             float& prevMarginBottom) {
    const auto& s = child->style;
    float mL = resolveMargin(s.marginLeft, availableWidth);
    float mR = resolveMargin(s.marginRight, availableWidth);
    float mT = resolveMargin(s.marginTop, availableWidth);
    float mB = resolveMargin(s.marginBottom, availableWidth);

    // 1. 计算子节点宽度
    float childW = availableWidth - mL - mR;
    if (!s.width.isAuto()) {
        float explicitW = s.width.toPx(availableWidth);
        childW = std::min(explicitW, childW);
    }
    if (!s.minWidth.isAuto())
        childW = std::max(childW, s.minWidth.toPx(availableWidth));
    if (!s.maxWidth.isAuto())
        childW = std::min(childW, s.maxWidth.toPx(availableWidth));
    childW = std::max(childW, 0.0f);

    // 2. 处理水平 margin auto 居中
    if (s.marginLeft.isAuto() && s.marginRight.isAuto()) {
        float remaining = availableWidth - childW;
        mL = remaining / 2;
        mR = remaining / 2;
    } else if (s.marginLeft.isAuto()) {
        mL = availableWidth - childW - mR;
    } else if (s.marginRight.isAuto()) {
        mR = availableWidth - childW - mL;
    }

    // 3. 垂直边距折叠（与上一个子节点的 margin-bottom 合并）
    float collapsedTop = std::max(prevMarginBottom, mT);
    y += collapsedTop;
    prevMarginBottom = mB;

    // 4. 确定子节点高度（可能为 auto）
    float childH = 0;
    bool heightAuto = s.height.isAuto();
    if (!heightAuto) {
        if (s.height.isPercent() && !isHeightDefined(child->parent)) {
            heightAuto = true;
        } else {
            childH = s.height.toPx(parentContentHeight);
            if (!s.minHeight.isAuto())
                childH = std::max(childH, s.minHeight.toPx(parentContentHeight));
            if (!s.maxHeight.isAuto())
                childH = std::min(childH, s.maxHeight.toPx(parentContentHeight));
            childH = std::max(childH, 0.0f);
        }
    }

    // 5. 设置位置（暂时）
    child->computed.x = x + mL;
    child->computed.y = y;

    // 6. 处理 box-sizing
    float borderPaddingX = child->paddingLeft + child->paddingRight
                         + child->borderLeft + child->borderRight;
    float borderPaddingY = child->paddingTop + child->paddingBottom
                         + child->borderTop + child->borderBottom;

    if (s.boxSizing == StyleClass::BorderBox) {
        child->computed.width = std::max(0.0f, childW - borderPaddingX);
        if (!heightAuto) {
            child->computed.height = std::max(0.0f, childH - borderPaddingY);
        } else {
            child->computed.height = 0;
        }
    } else { // ContentBox
        child->computed.width = childW;
        child->computed.height = heightAuto ? 0 : childH;
    }

    // 7. 先布局子节点（以便后续计算 auto 高度）
    layoutChildren(child, child->computed.width, child->computed.height);

    // 8. 若高度为 auto，根据子节点内容撑开
    if (heightAuto) {
        float contentH = 0;
        for (auto& gc : child->children) {
            if (!gc->isVisible()) continue;
            float gcBottom = gc->computed.y + gc->computed.height
                           + resolveMargin(gc->style.marginBottom, gc->computed.width);
            contentH = std::max(contentH, gcBottom);
        }
        float totalHeight = contentH + child->paddingTop + child->paddingBottom;
        if (s.boxSizing == StyleClass::BorderBox) {
            totalHeight += child->borderTop + child->borderBottom;
        }
        child->computed.height = totalHeight;
    }

    // 9. 更新当前块的 Y 坐标（加上子节点高度和下边距）
    y = child->computed.y + child->computed.height + mB;
}

// ========== 原 RenderNode 实例方法（改为调用静态布局） ==========
void RenderNode::layout(float parentW, float parentH) {
    if (!isVisible()) return;
    // 如果当前节点是根节点（parent == nullptr），调用根布局
    if (parent == nullptr) {
        canvas::layoutRoot(this, parentW, parentH);
    } else {
        // 普通节点：假设父节点已经设置好 computed 位置尺寸，只布局子节点
        canvas::layoutNode(this, parentW, parentH);
    }
}

void RenderNode::getBoundingRect(float& outX, float& outY, float& outW, float& outH) const {
    outX = 0; outY = 0;
    accumulateTransform(this, outX, outY);
    outW = computed.width;
    outH = computed.height;
}

ElementClass* RenderNode::hitTest(float px, float py) const {
    if (!isVisible()) return nullptr;
    for (int i = (int)children.size() - 1; i >= 0; i--) {
        ElementClass* result = children[i]->hitTest(px, py);
        if (result) return result;
    }
    float x, y, w, h;
    getBoundingRect(x, y, w, h);
    if (px >= x && px < x + w && py >= y && py < y + h) {
        return domNode;
    }
    return nullptr;
}

void RenderNode::paint(void* target, int targetW, int targetH, JSContext* ctx) {
    if (!isVisible()) return;
    for (auto& child : children) {
        if (child->isVisible()) {
            child->paint(target, targetW, targetH, ctx);
        }
    }
}

// ========== RenderCanvas 特殊处理 ==========
void RenderCanvas::layout(float parentW, float parentH) {
    RenderNode::layout(parentW, parentH);
    if (canvas) {
        if (computed.width <= 0 && canvas->width > 0)
            computed.width = canvas->width;
        if (computed.height <= 0 && canvas->height > 0)
            computed.height = canvas->height;
    }
}

void RenderCanvas::paint(void* target, int targetW, int targetH, JSContext* ctx) {
    if (!isVisible() || !canvas) return;
    auto context2d = canvas->context2d(ctx);
    if (context2d) {
        float x, y, w, h;
        getBoundingRect(x, y, w, h);
        context2d->composite(reinterpret_cast<uintptr_t>(target), targetW, targetH,
                             (int)0, (int)0, (int)w, (int)h,
                             computed.x, computed.y, computed.width, computed.height);
    }
    RenderNode::paint(target, targetW, targetH, ctx);
}

void RenderCanvas::getBoundingRect(float& outX, float& outY, float& outW, float& outH) const {
    outX = 0; outY = 0;
    accumulateTransform(this, outX, outY);
    float cssW = computed.width, cssH = computed.height;
    if (canvas) {
        outW = (cssW > 0) ? std::min(cssW, (float)canvas->width) : (float)canvas->width;
        outH = (cssH > 0) ? std::min(cssH, (float)canvas->height) : (float)canvas->height;
    } else {
        outW = cssW;
        outH = cssH;
    }
}

ElementClass* RenderCanvas::hitTest(float px, float py) const {
    for (int i = (int)children.size() - 1; i >= 0; i--) {
        ElementClass* result = children[i]->hitTest(px, py);
        if (result) return result;
    }
    float x, y, w, h;
    getBoundingRect(x, y, w, h);
    if (px >= x && px < x + w && py >= y && py < y + h) {
        return domNode;
    }
    return nullptr;
}

// ========== 树构建等辅助函数（与原始代码一致） ==========
static std::unique_ptr<RenderNode> createNodeForElement(JSContext* ctx, ElementClass* el) {
    if (!el) return nullptr;
    std::string tag = el->getTagName();
    std::unique_ptr<RenderNode> node;
    if (tag == "canvas") {
        CanvasClass* canvas = dynamic_cast<CanvasClass*>(el);
        if (canvas) {
            node = std::make_unique<RenderCanvas>(canvas, el);
        }
    }
    if (!node) {
        node = std::make_unique<RenderNode>();
    }
    node->domNode = el;
    node->style = *el->getInlineStyle(ctx);
    return node;
}

std::unique_ptr<RenderNode> buildRenderTree(JSContext* ctx, ElementClass* root) {
    if (!root) return nullptr;
    if (root->getInlineStyle(ctx)->display == StyleClass::DisplayNone) return nullptr;
    auto node = createNodeForElement(ctx, root);
    if (!node) return nullptr;
    int childCount = root->getChildCount(ctx);
    for (int i = 0; i < childCount; i++) {
        JSValue childVal = root->getChildAt(ctx, i);
        ElementClass* childEl = canvas::elementFromJS(ctx, childVal);
        if (!childEl && !JS_IsUndefined(childVal) && !JS_IsNull(childVal)) {
            JSValue tagValue = JS_GetPropertyStr(ctx, childVal, "tagName");
            const char* tagCStr = JS_ToCString(ctx, tagValue);
            const std::string tagName = tagCStr ? tagCStr : "";
            if (tagCStr) JS_FreeCString(ctx, tagCStr);
            JS_FreeValue(ctx, tagValue);
            if (tagName == "canvas") childEl = new canvas::CanvasClass();
            else childEl = new canvas::ElementClass(tagName);
            childEl->initJsFields(ctx, childVal);
        }
        if (childEl) {
            auto childNode = buildRenderTree(ctx, childEl);
            if (childNode) {
                childNode->parent = node.get();
                node->children.push_back(std::move(childNode));
            }
        }
        JS_FreeValue(ctx, childVal);
    }
    return node;
}

RenderNode* findRenderNode(RenderNode* root, ElementClass* el) {
    if (!root || !el) return nullptr;
    if (root->domNode == el) return root;
    for (auto& child : root->children) {
        auto* found = findRenderNode(child.get(), el);
        if (found) return found;
    }
    return nullptr;
}

} // namespace canvas