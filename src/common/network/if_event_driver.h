#ifndef COMMON_NETWORK_IF_EVENT_DRIVER
#define COMMON_NETWORK_IF_EVENT_DRIVER

#include <memory>
#include <vector>
#include <cstdint>

namespace quicx {
namespace common {

// Event types
enum class EventType {
    ET_READ  = 0x01,
    ET_WRITE = 0x02,
    ET_ERROR = 0x04,
    ET_CLOSE = 0x08
};

// Event structure
struct Event {
    int32_t fd = 0;
    EventType type = EventType::ET_READ;
    Event(int32_t socket, EventType type): fd(socket), type(type) {}
};

// Event driver interface
class IEventDriver {
public:
    virtual ~IEventDriver() = default;

    // Initialize the event driver
    virtual bool Init() = 0;

    // Add a file descriptor to monitor
    virtual bool AddFd(int32_t fd, EventType events) = 0;

    // Remove a file descriptor from monitoring
    virtual bool RemoveFd(int32_t fd) = 0;

    // Modify events for a file descriptor
    virtual bool ModifyFd(int32_t fd, EventType events) = 0;

    // Wait for events with timeout (in milliseconds)
    // Returns the number of events that occurred
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) = 0;

    // Get the maximum number of events that can be processed in one iteration
    virtual int GetMaxEvents() const = 0;

    // Wake up from Wait() call (thread-safe)
    virtual void Wakeup() = 0;

    // Create platform-specific event driver
    static std::unique_ptr<IEventDriver> Create();
};

} // namespace common
} // namespace quicx

#endif // COMMON_NETWORK_IF_EVENT_DRIVER 