#include "common/network/if_event_driver.h"

#ifdef _WIN32
    #include "common/network/windows/select_event_driver.h"
#elif defined(__APPLE__)
    #include "common/network/macos/kqueue_event_driver.h"
#else
    #include "common/network/linux/epoll_event_driver.h"
#endif

namespace quicx {
namespace common {

std::unique_ptr<IEventDriver> IEventDriver::Create() {
#ifdef _WIN32
    return std::unique_ptr<IEventDriver>(new SelectEventDriver());
#elif defined(__APPLE__)
    return std::unique_ptr<IEventDriver>(new KqueueEventDriver());
#else
    return std::unique_ptr<IEventDriver>(new EpollEventDriver());
#endif
}

}
}


