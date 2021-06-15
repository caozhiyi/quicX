#include "ack_range.h"

namespace quicx {

AckRange::AckRange():
    _smallest(0),
    _largest(0) {

}

AckRange::AckRange(uint64_t smallest, uint64_t largest):
    _smallest(smallest),
    _largest(largest) {

}

AckRange::~AckRange() {

}

uint16_t AckRange::Length() {
    return _largest - _smallest + 1;
}

}