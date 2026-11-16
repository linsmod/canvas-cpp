#pragma once

#include <string>

#include <quickjs.h>

namespace canvas {

struct Event {
    std::string type;
    bool bubbles = true;
    bool cancelable = false;

    explicit Event(const std::string& eventType = "") : type(eventType) {}
    virtual ~Event() = default;

    virtual JSValue toJS(JSContext* ctx) const;
};

struct UIEvent : Event {
    int detail = 0;

    explicit UIEvent(const std::string& eventType = "") : Event(eventType) {}
    JSValue toJS(JSContext* ctx) const override;
};

struct MouseEvent : UIEvent {
    int clientX = 0;
    int clientY = 0;
    int button = 0;
    int buttons = 0;
    bool ctrlKey = false;
    bool shiftKey = false;
    bool altKey = false;
    bool metaKey = false;

    explicit MouseEvent(const std::string& eventType = "") : UIEvent(eventType) {}
    JSValue toJS(JSContext* ctx) const override;
};

struct WheelEvent : MouseEvent {
    int deltaY = 0;

    explicit WheelEvent(const std::string& eventType = "") : MouseEvent(eventType) {}
    JSValue toJS(JSContext* ctx) const override;
};

struct KeyboardEvent : UIEvent {
    std::string key;
    bool ctrlKey = false;
    bool shiftKey = false;
    bool altKey = false;
    bool metaKey = false;

    explicit KeyboardEvent(const std::string& eventType = "") : UIEvent(eventType) {}
    JSValue toJS(JSContext* ctx) const override;
};

struct FocusEvent : UIEvent {
    bool focused = false;

    explicit FocusEvent(const std::string& eventType = "") : UIEvent(eventType) {}
    JSValue toJS(JSContext* ctx) const override;
};

JSValue eventToJS(JSContext* ctx, const Event& event);

} // namespace canvas
