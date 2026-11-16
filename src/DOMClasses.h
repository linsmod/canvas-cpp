#pragma once

#include <string>

#include <quickjs.h>

#include "Bindings.h"
#include "Context.h"
#include "ImageData.h"
#include "Layout.h"
#include "Style.h"

namespace canvas {

class NodeClass : public AnyClass {
    DECLARE_QUICKJS_CLASS(NodeClass);

public:
    explicit NodeClass(std::string className = "Node");
    ~NodeClass() override;

    const std::string& getClassName() const { return m_className; }

private:
    std::string m_className;
};

class ElementClass : public NodeClass {
    DECLARE_QUICKJS_CLASS(ElementClass);

public:
    explicit ElementClass(const std::string& tagName);
    ~ElementClass() override;

    const std::string& tagName() const { return m_tagName; }
    const std::string& getTagName() const { return m_tagName; }

    StyleClass* getInlineStyle(JSContext* ctx);
    int getChildCount(JSContext* ctx);
    JSValue getChildAt(JSContext* ctx, int index);
    void getBoundingRect(float& x, float& y, float& w, float& h) const;
    void setBoundingRect(float x, float y, float w, float h);

    void initJsFields(JSContext* ctx, JSValue object) override;
    void freeJsFields(JSRuntime* runtime) override;

private:
    JSValue m_jsValue = JS_UNDEFINED;
    std::string m_tagName;
    StyleClass m_inlineStyle;
    float m_rect[4] = {0, 0, 0, 0};
};

ElementClass* elementFromJS(JSContext* ctx, JSValueConst value);

class HtmlClass : public ElementClass {
    DECLARE_QUICKJS_CLASS(HtmlClass);

public:
    HtmlClass();

    void setDirtyListenr(canvas::RenderNode* listener);
    void setDirtyListener(canvas::RenderNode* listener) { setDirtyListenr(listener); }
    void markDirty();

private:
    canvas::RenderNode* m_dirtyListener = nullptr;
};

class CanvasClass : public ElementClass {
    DECLARE_QUICKJS_CLASS(CanvasClass);

public:
    CanvasClass();

    int width = 300;
    int height = 150;

    CanvasRenderingContext2d* context2d(JSContext* ctx);
    void resize(int width, int height);

private:
    CanvasRenderingContext2d* m_context = nullptr;
};

class DocumentClass : public NodeClass {
    DECLARE_QUICKJS_CLASS(DocumentClass);

public:
    DocumentClass();
    ~DocumentClass() override;

    HtmlClass* html() const { return m_html; }
    void setHtml(HtmlClass* html);

    void freeJsFields(JSRuntime* runtime) override;

private:
    HtmlClass* m_html = nullptr;
};

class WindowClass : public NodeClass {
    DECLARE_QUICKJS_CLASS(WindowClass);

public:
    WindowClass();
    ~WindowClass() override;

    DocumentClass* document() const { return m_document; }

    void initJsFields(JSContext* ctx, JSValue global) override;
    void freeJsFields(JSRuntime* runtime) override;

private:
    DocumentClass* m_document = nullptr;
};

class ImageClass : public NodeClass {
    DECLARE_QUICKJS_CLASS(ImageClass);

public:
    ImageClass();
    ImageData* data = nullptr;
};

} // namespace canvas
