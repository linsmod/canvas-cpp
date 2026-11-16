// Painter.h 修改部分

#include <set>   // 或者使用 multimap
#include <queue> // 或者 priority_queue

class Painter {
public:
    // ... 原有 public 方法 ...

    // 新的事件循环 tick：执行一个宏任务（到期的定时器），清空微任务
    // 返回值：true 表示还有待处理的工作（宏任务或微任务），false 表示空闲
    bool tick();

    // 执行所有 requestAnimationFrame 回调（在渲染前调用）
    void executeRAFCallbacks();

    // 查询是否有等待执行的 rAF 回调
    bool hasRAFCallbacks() const { return !m_rAFCallbacks.empty(); }

    // 查询是否有待处理的定时器（宏任务）
    bool hasPendingTimers() const { return !m_timerCallbacks.empty(); }

    // 保留原 update() 为兼容（内部可循环调用 tick 直到空闲）
    void update();  // 或者去掉，由外部调用 tick

    // 获取当前绝对时间（秒，用于定时器比较）
    double now() const { return m_absoluteTimer.seconds(); }

    // ... 其余原接口不变 ...

private:
    // 定时器回调结构（改为存储下次执行的绝对时间戳）
    struct TimerCallback {
        int id;
        JSValue function;     // 已 dup
        int intervalMs;       // interval 毫秒，0 表示一次性（setTimeout）
        double nextTime;      // 下次执行的绝对时间（秒）
        bool isInterval;      // true = setInterval, false = setTimeout

        TimerCallback(int id, JSValue func, int ms, bool interval, double nowSec)
            : id(id), function(func), intervalMs(ms), nextTime(nowSec + ms / 1000.0), isInterval(interval) {}
    };

    // 按下次执行时间排序的比较器（用于 std::multiset）
    struct CompareTimer {
        bool operator()(const TimerCallback& a, const TimerCallback& b) const {
            if (a.nextTime != b.nextTime) return a.nextTime < b.nextTime;
            return a.id < b.id; // 确保唯一性
        }
    };

    // 存储定时器的容器（有序，可用 std::multiset 或 std::priority_queue）
    std::set<TimerCallback, CompareTimer> m_timerCallbacks;
    int m_nextTimerId;                     // 自增 id

    // 绝对时间计时器（从不 reset，仅获取当前时间）
    Timer m_absoluteTimer;

    // 原有的 m_timer（已废弃，可删除或保留但不再用于定时器调度）
    // Timer m_timer;   // 可以删除，因为用绝对时间了

    // 微任务清空辅助函数
    void drainMicrotasks();

    // 执行单个定时器回调（由 tick 调用）
    void executeOneTimer();

    // ... 其他原有成员 (m_rAFCallbacks 等保留)
};