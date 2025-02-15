#include "quic/packet/if_packet.h"

namespace quicx {
namespace quic {

std::vector<std::shared_ptr<IFrame>>& IPacket::GetFrames() {
    static std::vector<std::shared_ptr<IFrame>> s_no_use;
    return s_no_use;
}

}
}
