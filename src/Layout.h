#pragma once

namespace canvas {

class RenderNode;

enum DirtyFlags {
    kDirtyNone = 0,
    CONTENT = 1,
    LAYOUT = 2,
    kDirtyAll = CONTENT | LAYOUT
};

void layoutTree(RenderNode* root, float viewportW, float viewportH);
void layoutRoot(RenderNode* root, float viewportW, float viewportH);
void layoutNode(RenderNode* node, float parentContentWidth, float parentContentHeight);

} // namespace canvas

using canvas::RenderNode;
