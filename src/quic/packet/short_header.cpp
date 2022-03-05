
#include "quic/packet/short_header.h"

namespace quicx {

ShortHeader::ShortHeader():
    _destination_connection_id(0),
    _packet_number(0) {
    _header_format._header = 0;
}

ShortHeader::~ShortHeader() {

}

bool ShortHeader::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    return true;
}

bool ShortHeader::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    return true;
}

uint32_t ShortHeader::EncodeSize() {
    return 0;
}

}
