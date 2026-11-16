#pragma once

#include <cstdint>
#include <vector>

namespace canvas {

// 固定网格的脏区跟踪器。只用于缩小最终 blit 区域，
// 不参与 DisplayList 的增量重放。
class Tiler {
public:
    explicit Tiler(int tileSize = 32);

    void setSize(float width, float height);
    void markDirty(float x, float y, float width, float height);
    void markAllDirty();
    void clearDirty();

    // 消费语义：合并当前所有脏 tile 后清空标记。
    void getDirtyBounds(int* x, int* y, int* width, int* height);

    bool hasDirty() const;
    float width() const { return m_width; }
    float height() const { return m_height; }
    int tileSize() const { return m_tileSize; }

private:
    void markTileRange(int tx0, int ty0, int tx1, int ty1);

    float m_width = 0.0f;
    float m_height = 0.0f;
    int m_tileSize = 32;
    int m_tilesX = 0;
    int m_tilesY = 0;
    std::vector<std::uint8_t> m_dirty;
};

} // namespace canvas
