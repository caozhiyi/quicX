#include "quic/udp/if_sender.h"
#include "quic/udp/udp_sender.h"

namespace quicx {
namespace quic {


std::shared_ptr<ISender> ISender::MakeSender(int32_t sockfd) {
    return std::make_shared<UdpSender>(sockfd);
}

}
}