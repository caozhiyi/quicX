#include "quic/packet/packet_interface.h"

namespace quicx {
namespace quic {

std::vector<std::shared_ptr<IFrame>>& IPacket::GetFrames() {
    static std::vector<std::shared_ptr<IFrame>> __no_use;
    return __no_use;
}

}
}
