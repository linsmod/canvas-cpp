#pragma once

#include <map>
#include <memory>
#include <string>

#include <quickjs.h>

#include "Bindings.h"
#include "DOMClasses.h"
#include "RenderNode.h"
#include "RenderThread.h"
#include "Script.h"
#include "util/Mutex.h"

namespace canvas {

class PlatformFileSystem;
class PlatformScreen;

class CanvasAppBase {
public:
    CanvasAppBase(int innerW, int innerH, int outerW, int outerH);
    virtual ~CanvasAppBase();

    virtual void setSize(int winWidth, int winHeight, int outerWidth, int outerHeight);
    virtual void makeCurrent() {}

    void setPageScale(float scale);
    void SetBaseDir(std::string dir);
    void runScript(const std::string& fileOrCode, bool isFile);
    void runModule(const std::string& entryFile);
    void TickScript();
    void buildRenderTreeAndPaint(uintptr_t targetFbo, int w, int h);

    Script* getScript();
    JSContext* getJsContext();
    RenderNode* renderTree() const { return m_renderTree.get(); }
    bool isDirty() const;
    int getScaledWidth();
    int getScaledHeight();

    void mapNodeSelf(NodeClass* node, JSValue value);
    JSValue getNodeJSValue(NodeClass* node) const;
    void unmapNodeSelf(NodeClass* node);
    JSValue getImage(const std::string& name);

    void dispatchMouseEvent(const char* type, int x, int y, int button, int buttons);
    void dispatchWheelEvent(const char* type, int x, int y, int deltaY,
                            bool ctrlKey, bool shiftKey, bool altKey, bool metaKey);
    void dispatchKeyEvent(const char* type, const char* key,
                          bool ctrlKey, bool shiftKey, bool altKey, bool metaKey);
    void dispatchWindowFocusEvent(bool focused);
    void dispatchVisibilityChanged(bool hidden);
    void dispatchOrientationChanged();

    int jsInnerWidth = 0;
    int jsInnerHeight = 0;
    int jsOuterWidth = 0;
    int jsOuterHeight = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    float dpiScale = 1.0f;
    float pageScale = 1.0f;

protected:
    std::string m_baseDir;
    PlatformFileSystem* m_fs = nullptr;
    PlatformScreen* m_screen = nullptr;
    Script* m_script = nullptr;
    JSRuntime* m_runtime = nullptr;
    JSContext* m_jsContext = nullptr;
    std::unique_ptr<RenderNode> m_renderTree;
    RenderThread* m_renderThread = nullptr;

    std::map<NodeClass*, JSValue> m_nodemapself;
    mutable Mutex m_nodemapMutex;
    std::map<std::string, ImageData*> m_images;

    float m_lastPageScale = 1.0f;
    bool m_paused = false;
    bool m_sizeDirty = false;
    bool m_scriptStarted = false;
    bool m_DOMContentLoaded_dispached = false;
    bool m_WindowLoad_dispached = false;
    int m_gcCounter = 0;
    uint64_t m_frameId = 0;
};

} // namespace canvas
