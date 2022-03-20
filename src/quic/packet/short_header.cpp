
#include "quic/packet/short_header.h"

namespace quicx {

ShortHeader::ShortHeader():
    _packet_number(0) {
    _header_format._header = 0;
}

ShortHeader::~ShortHeader() {

}

bool ShortHeader::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    return true;
}

bool ShortHeader::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    return true;
}

uint32_t ShortHeader::EncodeSize() {
    return 0;
}

}
