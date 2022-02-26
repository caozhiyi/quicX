#include "ack_range.h"

namespace quicx {

AckRange::AckRange():
    _gap(0),
    _ack_range_length(0) {

}

AckRange::AckRange(uint64_t gap, uint64_t range):
    _gap(gap),
    _ack_range_length(range) {

}

AckRange::~AckRange() {

}

}