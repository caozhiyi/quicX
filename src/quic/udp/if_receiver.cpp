#include "quic/udp/if_receiver.h"
#include "quic/udp/udp_receiver.h"

namespace quicx {
namespace quic {

std::shared_ptr<IReceiver> IReceiver::MakeReceiver(std::shared_ptr<common::IEventLoop> event_loop) {
    return std::make_shared<UdpReceiver>(event_loop);
}

}
}