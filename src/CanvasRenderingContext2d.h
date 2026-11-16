/**
 * CanvasRenderingContext2d - Picasso Backend Implementation
 * 
 * This provides a Cairo-like interface using the Picasso 2D graphics library.
 * Supports basic 2D drawing operations, transformations, text rendering,
 * and image manipulation with RGBA color format.
 */

#ifndef CANVAS_RENDERING_CONTEXT_2D_H
#define CANVAS_RENDERING_CONTEXT_2D_H

#include <string>
#include <vector>
#include <stack>

// Forward declarations for Picasso types
typedef struct ps_context_t ps_context_t;
typedef struct ps_canvas_t ps_canvas_t;
typedef struct ps_image_t ps_image_t;
typedef struct ps_matrix_t ps_matrix_t;
typedef struct ps_pattern_t ps_pattern_t;

class CanvasRenderingContext2d {
public:
    enum Format {
        kRGBA = 0,  // Standard RGBA byte order
        kARGB = 1   // Alpha in highest byte
    };

    CanvasRenderingContext2d(int width, int height, Format format = kRGBA);
    ~CanvasRenderingContext2d();

    // Disable copy
    CanvasRenderingContext2d(const CanvasRenderingContext2d&) = delete;
    CanvasRenderingContext2d& operator=(const CanvasRenderingContext2d&) = delete;

    void setSize(int width, int height);
    void copyImageTo(void* target);
    void composite(void* target, int targetW, int targetH);
    void composite(void* target, int targetW, int targetH,
                   int offsetX, int offsetY, int clipW, int clipH);

    // Styles
    float lineWidth() const;
    void setLineWidth(float width);
    std::string lineCap() const;
    void setLineCap(std::string const& cap);
    std::string strokeStyle() const;
    void setStrokeStyle(std::string const& style);
    std::string fillStyle() const;
    void setFillStyle(std::string const& style);

    struct GradientStop {
        float offset;
        struct { float r, g, b, a; } color;
    };
    struct GradientData {
        enum Type { LINEAR, RADIAL } type;
        float x0, y0, r0, x1, y1, r1;
        std::vector<GradientStop> stops;
    };
    void setFillGradient(GradientData const& grad);
    void setStrokeGradient(GradientData const& grad);

    float globalAlpha() const;
    void setGlobalAlpha(float alpha);
    std::string globalCompositeOperation() const;
    void setGlobalCompositeOperation(std::string const& op);

    // Transformations
    void scale(float x, float y);
    void rotate(float angle);
    void translate(float x, float y);
    void setTransform(float a, float b, float c, float d, float e, float f);
    void resetTransform();

    // State
    void save();
    void restore();

    // Text
    std::string font() const;
    void setFont(std::string const& fontStr);
    std::string textAlign() const;
    void setTextAlign(std::string const& align);
    std::string textBaseline() const;
    void setTextBaseline(std::string const& baseline);
    void fillText(std::string const& text, float x, float y);
    void strokeText(std::string const& text, float x, float y);
    void measureText(std::string const& text, double &outWidth, double &outAscent, double &outDescent);

    // Image operations
    class ImageData {
    public:
        ImageData(unsigned char* data, int w, int h);
        ~ImageData();
        int width() const { return m_width; }
        int height() const { return m_height; }
        unsigned char* data() const { return m_data; }
        ps_image_t* surface() const { return m_image; }
    private:
        int m_width, m_height;
        unsigned char* m_data;
        ps_image_t* m_image;
        bool m_ownsData;
    };
    ImageData* getImageData(float sx, float sy, float sw, float sh);
    void putImageData(ImageData const& imageData, float dx, float dy);
    void putImageData(ImageData const& imageData, float dx, float dy,
                      float dirtyX, float dirtyY, float dirtyW, float dirtyH);
    void drawImage(ImageData* image, float x, float y, float width, float height);

    // Drawing commands
    void beginPath();
    void closePath();
    void fill();
    void stroke();
    void clip();
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void quadraticCurveTo(float cpx, float cpy, float x, float y);
    void bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y);
    void arcTo(float x1, float y1, float x2, float y2, float radius);
    void rect(float x, float y, float width, float height);
    void roundRect(float x, float y, float width, float height, float radius);
    void arc(float x, float y, float radius, float startAngle, float endAngle, bool counterClockwise = false);
    void ellipse(float x, float y, float rx, float ry, float rotation,
                 float startAngle, float endAngle, bool counterClockwise = false);
    void fillRect(float x, float y, float width, float height);
    void strokeRect(float x, float y, float width, float height);
    void clear();
    void clearRect(float x, float y, float width, float height);

private:
    // Internal state tracking
    struct State {
        State();
        State(State const& other);
        ~State();

        // Style properties
        std::string fillStyleString;
        std::string strokeStyleString;
        std::string lineCapString;
        float lineWidth;
        float globalAlpha;
        std::string globalCompositeOperation;
        std::string shadowColor;
        float shadowBlur;
        bool imageSmoothingEnabled;
        std::string imageSmoothingQuality;
        std::string font;
        std::string textAlign;
        std::string textBaseline;

        // Gradient handling
        bool hasFillGradient;
        bool hasStrokeGradient;
        GradientData fillGradientData;
        GradientData strokeGradientData;

        // Helper
        ps_cairo_line_cap_t cairoCap() const;
    };

    State& currentState() { return m_stateStack.top(); }
    State const& currentState() const { return m_stateStack.top(); }

    void markDirty();
    void applyFillStyle();
    void applyStrokeStyle();
    void applyCompositeOperation();
    void applyFont();
    void syncTransform();
    void parseCssFont(std::string const& fontStr, std::string &family, std::string &weight, float &size);
    void applyGradient(GradientData const& grad, bool isStroke);
    ps_pattern_t* createCairoGradient(GradientData const& grad, float globalAlpha) const;

    ps_canvas_t* m_canvas;
    ps_context_t* m_cr;
    ps_matrix_t* m_transformMatrix;
    std::stack<State> m_stateStack;
    int m_width;
    int m_height;
    Format m_format;
    bool m_dirty;
};

#endif // CANVAS_RENDERING_CONTEXT_2D_H