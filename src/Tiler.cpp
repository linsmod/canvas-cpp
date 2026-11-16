#include "Tiler.h"

#include <algorithm>
#include <cmath>

namespace canvas {

Tiler::Tiler(int tileSize)
    : m_tileSize(tileSize > 0 ? tileSize : 32)
{
}

void Tiler::setSize(float width, float height)
{
    m_width = std::max(0.0f, width);
    m_height = std::max(0.0f, height);
    m_tilesX = static_cast<int>(std::ceil(m_width / static_cast<float>(m_tileSize)));
    m_tilesY = static_cast<int>(std::ceil(m_height / static_cast<float>(m_tileSize)));
    m_dirty.assign(static_cast<std::size_t>(m_tilesX) * m_tilesY, 0);
    markAllDirty();
}

void Tiler::markAllDirty()
{
    std::fill(m_dirty.begin(), m_dirty.end(), 1);
}

void Tiler::clearDirty()
{
    std::fill(m_dirty.begin(), m_dirty.end(), 0);
}

bool Tiler::hasDirty() const
{
    return std::find(m_dirty.begin(), m_dirty.end(), 1) != m_dirty.end();
}

void Tiler::markTileRange(int tx0, int ty0, int tx1, int ty1)
{
    if (m_tilesX <= 0 || m_tilesY <= 0) return;

    tx0 = std::max(tx0, 0);
    ty0 = std::max(ty0, 0);
    tx1 = std::min(tx1, m_tilesX - 1);
    ty1 = std::min(ty1, m_tilesY - 1);
    if (tx0 > tx1 || ty0 > ty1) return;

    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            m_dirty[static_cast<std::size_t>(ty) * m_tilesX + tx] = 1;
        }
    }
}

void Tiler::markDirty(float x, float y, float width, float height)
{
    if (width <= 0.0f || height <= 0.0f || m_tilesX <= 0 || m_tilesY <= 0) return;

    const float x0 = std::max(0.0f, x);
    const float y0 = std::max(0.0f, y);
    const float x1 = std::min(m_width, x + width);
    const float y1 = std::min(m_height, y + height);
    if (x1 <= x0 || y1 <= y0) return;

    const float tile = static_cast<float>(m_tileSize);
    const int tx0 = static_cast<int>(std::floor(x0 / tile));
    const int ty0 = static_cast<int>(std::floor(y0 / tile));
    const int tx1 = static_cast<int>(std::ceil(x1 / tile)) - 1;
    const int ty1 = static_cast<int>(std::ceil(y1 / tile)) - 1;

    markTileRange(tx0, ty0, tx1, ty1);
}

void Tiler::getDirtyBounds(int* x, int* y, int* width, int* height)
{
    bool any = false;
    int left = 0, top = 0, right = 0, bottom = 0;

    for (int ty = 0; ty < m_tilesY; ++ty) {
        for (int tx = 0; tx < m_tilesX; ++tx) {
            if (!m_dirty[static_cast<std::size_t>(ty) * m_tilesX + tx]) continue;

            const int tileLeft = tx * m_tileSize;
            const int tileTop = ty * m_tileSize;
            const int tileRight = std::min(tileLeft + m_tileSize,
                                           static_cast<int>(std::ceil(m_width)));
            const int tileBottom = std::min(tileTop + m_tileSize,
                                            static_cast<int>(std::ceil(m_height)));
            if (tileRight <= tileLeft || tileBottom <= tileTop) continue;

            if (!any) {
                left = tileLeft;
                top = tileTop;
                right = tileRight;
                bottom = tileBottom;
                any = true;
            } else {
                left = std::min(left, tileLeft);
                top = std::min(top, tileTop);
                right = std::max(right, tileRight);
                bottom = std::max(bottom, tileBottom);
            }
        }
    }

    if (x) *x = any ? left : 0;
    if (y) *y = any ? top : 0;
    if (width) *width = any ? right - left : 0;
    if (height) *height = any ? bottom - top : 0;

    clearDirty();
}

} // namespace canvas
