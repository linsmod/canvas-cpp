#pragma once

#include <cairo.h>
#include <vector>
#include "Bindings.h"

namespace canvas {

class ImageData {
    DECLARE_QUICKJS_CLASS(ImageData);

public:
    // 从 Cairo 表面创建（只读时会提取数据）
    ImageData(cairo_surface_t* surface);
    // 从外部 RGBA 数据创建（深拷贝）
    ImageData(const uint8_t* rgbaData, int width, int height);
    // 拷贝构造（深拷贝）
    ImageData(const ImageData& other);
    ~ImageData();

    int width() const { return m_width; }
    int height() const { return m_height; }

    // 直接访问 RGBA 数据（可读写）
    uint8_t* data() { return m_rgbaData.data(); }
    const uint8_t* data() const { return m_rgbaData.data(); }
    size_t dataSize() const { return m_rgbaData.size(); }

    // 用于绘制：返回 Cairo 表面（BGRA 格式）
    cairo_surface_t* surface();

    // 将当前 RGBA 数据同步到 Cairo 表面（在绘制前调用）
    void syncToSurface();

private:
    void initFromSurface();          // 从 Cairo 表面读取并转换为 RGBA
    void createSurfaceFromRGBA();    // 根据 RGBA 数据创建/更新 Cairo 表面

    cairo_surface_t* m_surface;
    std::vector<uint8_t> m_rgbaData;
    int m_width;
    int m_height;
    bool m_surfaceDirty;             // 标记表面是否需要更新
};

} // namespace canvas