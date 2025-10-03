#include "common/network/event_loop.h"
#include "common/network/if_event_loop.h"

namespace quicx {
namespace common {

std::shared_ptr<IEventLoop> MakeEventLoop() {
    return std::shared_ptr<IEventLoop>(new EventLoop());
}

}
}


