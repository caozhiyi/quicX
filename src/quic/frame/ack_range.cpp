#include "quic/frame/ack_range.h"

namespace quicx {
namespace quic {

AckRange::AckRange():
    gap_(0),
    ack_range_length_(0) {

}

AckRange::AckRange(uint64_t gap, uint64_t range):
    gap_(gap),
    ack_range_length_(range) {

}

AckRange::~AckRange() {

}

}
}