#include "quic/udp/if_sender.h"
#include "quic/udp/udp_sender.h"

namespace quicx {
namespace quic {


std::shared_ptr<ISender> ISender::MakeSender() {
    return std::make_shared<UdpSender>();
}

}
}