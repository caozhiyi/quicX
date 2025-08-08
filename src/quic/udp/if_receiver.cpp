#include "quic/udp/if_receiver.h"
#include "quic/udp/udp_receiver.h"

namespace quicx {
namespace quic {

std::shared_ptr<IReceiver> IReceiver::MakeReceiver() {
    return std::make_shared<UdpReceiver>();
}

}
}