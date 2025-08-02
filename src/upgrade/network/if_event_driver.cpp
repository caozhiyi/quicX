#include "upgrade/network/if_event_driver.h"

#ifdef _WIN32
    #include "upgrade/network/windows/iocp_event_driver.h"
#elif defined(__APPLE__)
    #include "upgrade/network/macos/kqueue_event_driver.h"
#else
    #include "upgrade/network/linux/epoll_event_driver.h"
#endif

namespace quicx {
namespace upgrade {

std::unique_ptr<IEventDriver> IEventDriver::Create() {
#ifdef _WIN32
    return std::make_unique<IocpEventDriver>();
#elif defined(__APPLE__)
    return std::make_unique<KqueueEventDriver>();
#else
    return std::make_unique<EpollEventDriver>();
#endif
}

} // namespace upgrade
} // namespace quicx 