#include "ImageData.h"
#include "Bindings.h"
#include "Log.h"
#include "cairo.h"
#include <cassert>
#include <cstring>

namespace canvas {

    IMPL_QUICKJS_CLASS(ImageData);
static void bgra_to_rgba(const uint8_t* src, uint8_t* dst, int pixels) {
    for (int i = 0; i < pixels; ++i) {
        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
        src += 4; dst += 4;
    }
}

static void rgba_to_bgra(const uint8_t* src, uint8_t* dst, int pixels) {
    for (int i = 0; i < pixels; ++i) {
        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
        src += 4; dst += 4;
    }
}

ImageData::ImageData(cairo_surface_t* surface)
    : m_surface(surface)
    , m_width(cairo_image_surface_get_width(surface))
    , m_height(cairo_image_surface_get_height(surface))
    , m_surfaceDirty(false)
{
    cairo_surface_reference(m_surface);
    initFromSurface();
}

ImageData::ImageData(const uint8_t* rgbaData, int width, int height)
    : m_surface(nullptr)
    , m_rgbaData(rgbaData, rgbaData + width * height * 4)
    , m_width(width)
    , m_height(height)
    , m_surfaceDirty(true)
{
    createSurfaceFromRGBA();
}

ImageData::ImageData(const ImageData& other)
    : m_surface(nullptr)
    , m_rgbaData(other.m_rgbaData)
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_surfaceDirty(true)
{
    createSurfaceFromRGBA();
}

ImageData::~ImageData() {
    if (m_surface) cairo_surface_destroy(m_surface);
}

static char* image_format_toString(cairo_format_t fmt){
    static char* fmtstrs[]{
     "CAIRO_FORMAT_INVALID"   // -1,
    ,"CAIRO_FORMAT_ARGB32"    // 0,
    ,"CAIRO_FORMAT_RGB24"     // 1,
    ,"CAIRO_FORMAT_A8"        // 2,
    ,"CAIRO_FORMAT_A1"        // 3,
    ,"CAIRO_FORMAT_RGB16_565" // 4,
    ,"CAIRO_FORMAT_RGB30"     // 5,
    ,"CAIRO_FORMAT_RGB96F"    // 6,
    ,"CAIRO_FORMAT_RGBA128F"  // 7
    };
    return fmtstrs[fmt+1];
}

void ImageData::initFromSurface() {
    cairo_surface_flush(m_surface);
    uint8_t* src = cairo_image_surface_get_data(m_surface);
    int stride = cairo_image_surface_get_stride(m_surface);
    m_rgbaData.resize(m_width * m_height * 4);
    for (int row = 0; row < m_height; ++row) {
        const uint8_t* srcRow = src + row * stride;
        uint8_t* dstRow = m_rgbaData.data() + row * m_width * 4;
        // when native-endian，bgra==CAIRO_FORMAT_ARGB32
        bgra_to_rgba(srcRow, dstRow, m_width);
    }
    LOGI("ImageData created: size=%dx%d, surface format=%s",m_width,m_height,image_format_toString(cairo_image_surface_get_format(surface())));
    m_surfaceDirty = false;
}

void ImageData::createSurfaceFromRGBA() {
    if (m_surface) cairo_surface_destroy(m_surface);
    m_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, m_width, m_height);
    cairo_surface_flush(m_surface);
    uint8_t* dst = cairo_image_surface_get_data(m_surface);
    int stride = cairo_image_surface_get_stride(m_surface);
    for (int row = 0; row < m_height; ++row) {
        const uint8_t* srcRow = m_rgbaData.data() + row * m_width * 4;
        uint8_t* dstRow = dst + row * stride;
        rgba_to_bgra(srcRow, dstRow, m_width);
    }
    cairo_surface_mark_dirty(m_surface);
    m_surfaceDirty = false;
}

cairo_surface_t* ImageData::surface() {
    if (m_surfaceDirty) createSurfaceFromRGBA();
    return m_surface;
}

void ImageData::syncToSurface() {
    if (m_surfaceDirty) createSurfaceFromRGBA();
}

} // namespace canvas