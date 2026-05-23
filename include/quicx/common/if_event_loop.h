#ifndef COMMON_NETWORK_IF_EVENT_LOOP
#define COMMON_NETWORK_IF_EVENT_LOOP

#include <memory>
#include <cstdint>
#include <functional>

namespace quicx {
namespace common {

// Forward declarations of timer types. Their full definitions live in
// internal headers (src/common/timer/{if_timer,timer_task}.h) and are not
// part of the public API. IEventLoop only references them by reference or
// std::shared_ptr, so a forward declaration is sufficient for compilation.
class ITimer;
class TimerTask;

/**
 * @brief File descriptor event handler interface
 */
class IFdHandler {
public:
    virtual ~IFdHandler() = default;

    /**
     * @brief Called when the file descriptor is readable
     *
     * @param fd File descriptor
     */
    virtual void OnRead(uint32_t fd) = 0;

    /**
     * @brief Called when the file descriptor is writable
     *
     * @param fd File descriptor
     */
    virtual void OnWrite(uint32_t fd) = 0;

    /**
     * @brief Called when an error occurs on the file descriptor
     *
     * @param fd File descriptor
     */
    virtual void OnError(uint32_t fd) = 0;

    /**
     * @brief Called when the file descriptor is closed
     *
     * @param fd File descriptor
     */
    virtual void OnClose(uint32_t fd) = 0;
};

/**
 * @brief Event loop interface for I/O multiplexing and timer management
 *
 * Provides epoll/kqueue-based event processing and timer scheduling.
 */
class IEventLoop {
public:
    virtual ~IEventLoop() = default;

    /**
     * @brief Initialize the event loop
     *
     * @return true if initialized successfully, false otherwise
     */
    virtual bool Init() = 0;

    /**
     * @brief Wait for events
     *
     * @return Number of events processed, or -1 on error
     */
    virtual int Wait() = 0;

    /**
     * @brief Register a file descriptor for event monitoring
     *
     * @param fd File descriptor to monitor
     * @param events Event mask (EPOLLIN, EPOLLOUT, etc.)
     * @param handler Handler for events on this fd
     * @return true if registered successfully, false otherwise
     */
    virtual bool RegisterFd(uint32_t fd, int32_t events, std::shared_ptr<IFdHandler> handler) = 0;

    /**
     * @brief Modify event mask for a registered file descriptor
     *
     * @param fd File descriptor
     * @param events New event mask
     * @return true if modified successfully, false otherwise
     */
    virtual bool ModifyFd(uint32_t fd, int32_t events) = 0;

    /**
     * @brief Remove a file descriptor from monitoring
     *
     * @param fd File descriptor to remove
     * @return true if removed successfully, false otherwise
     */
    virtual bool RemoveFd(uint32_t fd) = 0;

    /**
     * @brief Add a callback that runs on every event loop iteration
     *
     * @param cb Callback function
     * @deprecated Use the overload with weak_ptr<void> owner instead.
     *             This overload captures a strong callback that may embed
     *             shared_ptr to the caller, creating reference cycles.
     */
    virtual void AddFixedProcess(std::function<void()> cb) = 0;

    /**
     * @brief Add a lifetime-guarded callback that runs on every event loop iteration.
     *
     * The callback will only execute while `owner` is still alive.
     * Once the owner is destroyed, the callback is automatically skipped and
     * eventually removed — no manual ClearFixedProcesses() needed.
     *
     * @param owner  Weak reference to the object whose lifetime guards this callback.
     *               When owner.lock() returns nullptr the callback is dead.
     * @param cb     Callback function to execute each iteration.
     */
    virtual void AddFixedProcess(std::weak_ptr<void> owner, std::function<void()> cb) = 0;

    /**
     * @brief Clear all fixed-process callbacks previously added via
     *        AddFixedProcess().
     *
     * @deprecated With the weak_ptr-guarded AddFixedProcess overload,
     *             manual clearing is no longer necessary. Kept for
     *             backward compatibility during the migration period.
     */
    virtual void ClearFixedProcesses() = 0;

    /**
     * @brief Add a timer with callback
     *
     * @param cb Callback function
     * @param delay_ms Delay in milliseconds
     * @param repeat Whether to repeat the timer
     * @return Timer ID
     */
    virtual uint64_t AddTimer(std::function<void()> cb, uint32_t delay_ms, bool repeat = false) = 0;

    /**
     * @brief Add a timer task
     *
     * @param task Timer task object
     * @param delay_ms Delay in milliseconds
     * @param repeat Whether to repeat the timer
     * @return Timer ID
     */
    virtual uint64_t AddTimer(TimerTask& task, uint32_t delay_ms, bool repeat = false) = 0;

    /**
     * @brief Remove a timer by ID
     *
     * @param timer_id Timer ID returned by AddTimer
     * @return true if removed successfully, false otherwise
     */
    virtual bool RemoveTimer(uint64_t timer_id) = 0;

    /**
     * @brief Remove a timer task
     *
     * @param task Timer task to remove
     * @return true if removed successfully, false otherwise
     */
    virtual bool RemoveTimer(TimerTask& task) = 0;

    /**
     * @brief Remove every pending timer and drop all callback closures.
     *
     * Motivation: BaseConnection's Closing/Draining paths each register
     *   event_loop_->AddTimer([self = shared_from_this()]() {
     *       self->OnClosingTimeout();
     *   }, 3 * GetCloseWaitTime(), false);
     * That closure captures a shared_ptr<BaseConnection>; the EventLoop's
     * timers_ map keeps the captured closure alive until the timer fires.
     * If the owning QuicClient/QuicServer stops the master event loop
     * before the timer fires, the callback never runs — and
     * ~BaseConnection() never runs, leaking ~120KB per connection cycle.
     *
     * Call this after the event loop has been stopped and joined, right
     * before (or as part of) owner teardown, to release every pending
     * timer closure deterministically.
     */
    virtual void ClearAllTimers() = 0;

    /**
     * @brief Post a task to run in the event loop thread
     *
     * @param fn Task function
     */
    virtual void PostTask(std::function<void()> fn) = 0;

    /**
     * @brief Wake up the event loop
     */
    virtual void Wakeup() = 0;

    /**
     * @brief Get the timer instance
     *
     * @return Timer instance
     */
    virtual std::shared_ptr<ITimer> GetTimer() = 0;

    /**
     * @brief Set custom timer for testing
     *
     * @param timer Timer instance
     */
    virtual void SetTimerForTest(std::shared_ptr<ITimer> timer) = 0;

    /**
     * @brief Check if the current thread is the event loop thread
     *
     * @return true if in loop thread, false otherwise
     */
    virtual bool IsInLoopThread() const = 0;

    /**
     * @brief Run a task in the event loop thread
     *
     * @param task Task function
     */
    virtual void RunInLoop(std::function<void()> task) = 0;

    /**
     * @brief Assert that the current thread is the event loop thread
     */
    virtual void AssertInLoopThread() = 0;
};

/**
 * @brief Create a default event loop instance
 *
 * @return Event loop instance
 */
std::shared_ptr<IEventLoop> MakeEventLoop();

}  // namespace common
}  // namespace quicx

#endif  // COMMON_NETWORK_IF_EVENT_LOOP
