#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <quickjs.h>

#include "Bindings.h"
#include "DOMClasses.h"
#include "util/Timer.h"

namespace canvas {

struct TimerCallback {
    int id = 0;
    JSValue function = JS_UNDEFINED;
    int intervalMs = 0;
    bool isInterval = false;
    double nextTime = 0.0;

    TimerCallback() = default;
    TimerCallback(int callbackId, JSValue callback, int interval, bool repeat, double due)
        : id(callbackId), function(callback), intervalMs(interval),
          isInterval(repeat), nextTime(due)
    {
    }

    bool operator<(const TimerCallback& other) const
    {
        if (nextTime != other.nextTime) return nextTime < other.nextTime;
        return id < other.id;
    }
};

class Script {
public:
    explicit Script(JSContext* context);
    ~Script();

    bool runString(const std::string& code, const std::string& filename = "<input>");
    bool runModule(const std::string& code, const std::string& filename);
    bool load(const std::string& filename);
    bool load(const std::vector<std::string>& files);

    bool tickScript(int iterations = 10);
    bool executeRAFCallbacks();
    bool executeOneTimer();
    bool drainMicrotasks();

    int setTimeout(JSValue function, int delay);
    int setInterval(JSValue function, int interval);
    void clearTimeout(int id);
    void clearInterval(int id);
    int requestAnimationFrame(JSValue function);
    void cancelAnimationFrame(int id);

    void runGc(std::string msg);
    JSValue getObject(const std::string& name);
    WindowClass* window();

private:
    JSContext* m_context;
    JSRuntime* m_runtime;
    std::multiset<TimerCallback> m_timerCallbacks;
    std::map<int, JSValue> m_rAFCallbacks;
    Timer m_absoluteTimer;
    Protos m_protos;
    WindowClass* m_window;
};

} // namespace canvas
