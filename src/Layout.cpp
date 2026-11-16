#include "Layout.h"
#include "RenderNode.h"
#include <algorithm>

namespace canvas {

static float resolveMargin(const CSSValue& margin, float parentContentSize) {
    if (margin.isAuto()) return 0;
    return margin.toPx(parentContentSize);
}

static void layoutBlock(RenderNode* node, float parentContentW, float parentContentH) {
    float x = node->computed.paddingLeft;
    float y = node->computed.paddingTop;
    float availableW = std::max(0.0f, parentContentW
        - node->computed.paddingLeft - node->computed.paddingRight);
    float availableH = std::max(0.0f, parentContentH
        - node->computed.paddingTop - node->computed.paddingBottom);

    for (auto& child : node->children) {
        if (!child->isVisible()) continue;

        auto& s = child->style;
        float mL = resolveMargin(s.marginLeft, availableW);
        float mR = resolveMargin(s.marginRight, availableW);
        float mT = resolveMargin(s.marginTop, availableW);
        float mB = resolveMargin(s.marginBottom, availableW);

        // Resolve child width
        float childW = availableW - mL - mR;
        if (!s.width.isAuto()) {
            float explicitW = s.width.toPx(availableW);
            childW = std::min(explicitW, childW);
            childW = std::max(childW, 0.0f);
        }
        // Apply min/max
        if (!s.minWidth.isAuto()) childW = std::max(childW, s.minWidth.toPx(availableW));
        if (!s.maxWidth.isAuto()) childW = std::min(childW, s.maxWidth.toPx(availableW));

        // Resolve child height
        float childH = 0;
        if (!s.height.isAuto()) {
            childH = s.height.toPx(availableH);
            if (!s.minHeight.isAuto()) childH = std::max(childH, s.minHeight.toPx(availableH));
            if (!s.maxHeight.isAuto()) childH = std::min(childH, s.maxHeight.toPx(availableH));
            childH = std::max(childH, 0.0f);
        }

        child->computed.x = x + mL;
        child->computed.y = y + mT;

        if (s.boxSizing == StyleClass::BorderBox) {
            float borderPadX = child->computed.paddingLeft + child->computed.paddingRight
                             + child->computed.borderLeft + child->computed.borderRight;
            float borderPadY = child->computed.paddingTop + child->computed.paddingBottom
                             + child->computed.borderTop + child->computed.borderBottom;
            child->computed.width = std::max(0.0f, childW - borderPadX);
            child->computed.width += borderPadX;
            if (!s.height.isAuto()) {
                child->computed.height = std::max(0.0f, childH - borderPadY);
                child->computed.height += borderPadY;
            }
        } else {
            child->computed.width = childW;
            child->computed.height = childH;
        }

        // Layout children recursively
        child->layout(child->computed.width, child->computed.height);

        // Auto height: compute from content
        if (s.height.isAuto()) {
            float contentH = 0;
            for (auto& gc : child->children) {
                if (!gc->isVisible()) continue;
                contentH = std::max(contentH,
                    gc->computed.y + gc->computed.height
                    + resolveMargin(gc->style.marginBottom, gc->computed.width)
                    - child->computed.paddingTop);
            }
            float padY = child->computed.paddingTop + child->computed.paddingBottom;
            child->computed.height = std::max(0.0f, contentH + padY);
        }

        y = child->computed.y + child->computed.height + mB;
    }

    // Update own height if auto
    if (node->style.height.isAuto() && !node->children.empty()) {
        node->computed.height = y + node->computed.paddingBottom;
    }
}

static void layoutAbsolute(RenderNode* node, float containerW, float containerH) {
    auto& s = node->style;

    float boxW = containerW - node->computed.paddingLeft - node->computed.paddingRight;
    float boxH = containerH - node->computed.paddingTop - node->computed.paddingBottom;

    // Resolve width
    float w = boxW;
    if (!s.width.isAuto()) {
        w = s.width.toPx(boxW);
    }
    if (!s.minWidth.isAuto()) w = std::max(w, s.minWidth.toPx(boxW));
    if (!s.maxWidth.isAuto()) w = std::min(w, s.maxWidth.toPx(boxW));

    // Resolve height
    float h = 0;
    if (!s.height.isAuto()) {
        h = s.height.toPx(boxH);
    }

    // Resolve position
    float x = node->computed.paddingLeft;
    float y = node->computed.paddingTop;

    if (!s.left.isAuto()) x += s.left.toPx(boxW);
    else if (!s.right.isAuto()) x = containerW - node->computed.paddingRight - s.right.toPx(boxW) - w;

    if (!s.top.isAuto()) y += s.top.toPx(boxH);
    else if (!s.bottom.isAuto()) y = containerH - node->computed.paddingBottom - s.bottom.toPx(boxH) - h;

    node->computed.x = x;
    node->computed.y = y;
    node->computed.width = w;
    node->computed.height = h;

    // Layout children with block flow within the positioned element
    node->layout(w, h);
}

void layoutTree(RenderNode* root, float viewportW, float viewportH) {
    if (!root || !root->isVisible()) return;

    // Root padding = 0 (body has no padding by default)
    root->computed.paddingLeft = 0;
    root->computed.paddingRight = 0;
    root->computed.paddingTop = 0;
    root->computed.paddingBottom = 0;

    // Root fills viewport
    root->computed.x = 0;
    root->computed.y = 0;
    root->computed.width = viewportW;
    root->computed.height = viewportH;

    // Root style dimensions override viewport
    if (!root->style.width.isAuto()) {
        root->computed.width = root->style.width.toPx(viewportW);
    }
    if (!root->style.height.isAuto()) {
        root->computed.height = root->style.height.toPx(viewportH);
    }

    // Layout each child with block flow
    float x = 0;
    float y = 0;
    for (auto& child : root->children) {
        if (!child->isVisible()) continue;

        auto& s = child->style;
        float mL = resolveMargin(s.marginLeft, root->computed.width);
        float mR = resolveMargin(s.marginRight, root->computed.width);
        float mT = resolveMargin(s.marginTop, root->computed.width);
        float mB = resolveMargin(s.marginBottom, root->computed.width);

        if (s.position == StyleClass::Absolute || s.position == StyleClass::Fixed) {
            layoutAbsolute(child.get(), root->computed.width, root->computed.height);
            continue;
        }

        float childW = root->computed.width - mL - mR;
        if (!s.width.isAuto()) {
            float explicitW = s.width.toPx(root->computed.width);
            childW = std::min(explicitW, childW);
        }
        if (!s.minWidth.isAuto()) childW = std::max(childW, s.minWidth.toPx(root->computed.width));
        if (!s.maxWidth.isAuto()) childW = std::min(childW, s.maxWidth.toPx(root->computed.width));
        childW = std::max(childW, 0.0f);

        float childH = 0;
        if (!s.height.isAuto()) {
            childH = s.height.toPx(root->computed.height);
            if (!s.minHeight.isAuto()) childH = std::max(childH, s.minHeight.toPx(root->computed.height));
            if (!s.maxHeight.isAuto()) childH = std::min(childH, s.maxHeight.toPx(root->computed.height));
            childH = std::max(childH, 0.0f);
        }

        child->computed.x = mL;
        child->computed.y = y + mT;

        if (s.boxSizing == StyleClass::BorderBox) {
            float bpX = child->computed.paddingLeft + child->computed.paddingRight
                      + child->computed.borderLeft + child->computed.borderRight;
            float bpY = child->computed.paddingTop + child->computed.paddingBottom
                      + child->computed.borderTop + child->computed.borderBottom;
            child->computed.width = std::max(0.0f, childW - bpX);
            child->computed.width += bpX;
            if (!s.height.isAuto()) {
                child->computed.height = std::max(0.0f, childH - bpY);
                child->computed.height += bpY;
            }
        } else {
            child->computed.width = childW;
            child->computed.height = childH;
        }

        child->layout(child->computed.width, child->computed.height);

        if (s.height.isAuto()) {
            float contentH = 0;
            for (auto& gc : child->children) {
                if (!gc->isVisible()) continue;
                contentH = std::max(contentH,
                    gc->computed.y + gc->computed.height
                    + resolveMargin(gc->style.marginBottom, gc->computed.width)
                    - child->computed.paddingTop);
            }
            float padY = child->computed.paddingTop + child->computed.paddingBottom;
            child->computed.height = std::max(0.0f, contentH + padY);
        }

        y = child->computed.y + child->computed.height + mB;
    }
}

} // namespace canvas