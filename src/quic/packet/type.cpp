#include "quic/packet/type.h"
#include "quic/common/constants.h"

namespace quicx {

bool IsLongHeaderPacket(uint8_t flag) {
    return flag&__long_header_form > 0;
}

bool IsShortHeaderPacket(uint8_t flag) {
    return flag&__long_header_form == 0;
}

}