#include "common/network/event_loop.h"
#include "common/network/if_event_loop.h"

namespace quicx {
namespace common {

std::unique_ptr<IEventLoop> MakeEventLoop() {
    return std::unique_ptr<IEventLoop>(new EventLoop());
}

}
}


