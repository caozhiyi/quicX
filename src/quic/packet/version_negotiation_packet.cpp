#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {


VersionNegotiationPacket::VersionNegotiationPacket() {
    _header_format._header_info._header_form = 1;
    _version = 0;
}

VersionNegotiationPacket::~VersionNegotiationPacket() {
    
}


bool VersionNegotiationPacket::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    LongHeader::Encode(buffer);

    if (_support_version.size() > 0) {
        buffer->Write((char*)&(*_support_version.begin()), _support_version.size() * sizeof(uint32_t));
    }
    return true;
}

bool VersionNegotiationPacket::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    LongHeader::Decode(buffer);
    uint32_t size = buffer->GetCanReadLength();
    if (size % sizeof(uint32_t) > 0) {
        // error size of version negotiation packet.
        // TODO send error info.
        return false;
    }

    uint32_t version_list_size = size / sizeof(uint32_t);
    _support_version.resize(version_list_size);

    buffer->Read((char*)&(*_support_version.begin()), size);
    return true;
}

uint32_t VersionNegotiationPacket::EncodeSize() {
    return sizeof(VersionNegotiationPacket) + _support_version.size() * sizeof(uint32_t);
}

bool VersionNegotiationPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    // do nothing
    return true;
}

}
