
#include "quic/packet/header/short_header.h"

namespace quicx {

ShortHeader::ShortHeader() {

}

ShortHeader::ShortHeader(uint8_t flag):
    IHeader(flag) {
}

ShortHeader::~ShortHeader() {

}

bool ShortHeader::EncodeHeader(std::shared_ptr<IBufferWrite> buffer) {
    return true;
}

bool ShortHeader::DecodeHeader(std::shared_ptr<IBufferRead> buffer, bool with_type) {
    return true;
}

uint32_t ShortHeader::EncodeHeaderSize() {
    return 0;
}

}
