#ifndef UPGRADE_NETWORK_IF_EVENT_DRIVER_H
#define UPGRADE_NETWORK_IF_EVENT_DRIVER_H

#include <functional>
#include <memory>
#include <cstdint>
#include <vector>

namespace quicx {
namespace upgrade {

// Event types
enum class EventType {
    READ = 0x01,
    WRITE = 0x02,
    ERROR = 0x04,
    CLOSE = 0x08
};

// Event structure
struct Event {
    int fd = -1;
    EventType type = EventType::READ;
    void* user_data = nullptr;
};

// Event driver interface
class IEventDriver {
public:
    virtual ~IEventDriver() = default;

    // Initialize the event driver
    virtual bool Init() = 0;

    // Add a file descriptor to monitor
    virtual bool AddFd(int fd, EventType events, void* user_data = nullptr) = 0;

    // Remove a file descriptor from monitoring
    virtual bool RemoveFd(int fd) = 0;

    // Modify events for a file descriptor
    virtual bool ModifyFd(int fd, EventType events, void* user_data = nullptr) = 0;

    // Wait for events with timeout (in milliseconds)
    // Returns the number of events that occurred
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) = 0;

    // Get the maximum number of events that can be processed in one iteration
    virtual int GetMaxEvents() const = 0;

    // Create platform-specific event driver
    static std::unique_ptr<IEventDriver> Create();
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_IF_EVENT_DRIVER_H 