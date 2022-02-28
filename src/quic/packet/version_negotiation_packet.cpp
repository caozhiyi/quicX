#include "version_negotiation_packet.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {


VersionNegotiationPacket::VersionNegotiationPacket() {
    _header_format._header_info._header_form = 1;
    _version = 0;
}

VersionNegotiationPacket::~VersionNegotiationPacket() {
    
}


bool VersionNegotiationPacket::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    LongHeaderPacket::Encode(buffer, alloter);

    if (_support_version.size() > 0) {
        buffer->Write((char*)&(*_support_version.begin()), _support_version.size() * sizeof(uint32_t));
    }
    return true;
}

bool VersionNegotiationPacket::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    LongHeaderPacket::Decode(buffer, alloter);
    uint32_t size = buffer->GetCanReadLength();
    if (size % sizeof(uint32_t) > 0) {
        // error size of version negotiation packet.
        // TODO send error info.
        return false;
    }

    uint32_t version_list_size = size / sizeof(uint32_t);
    _support_version.resize(version_list_size);
    
    char* data = alloter->PoolMalloc<char>(size);
    buffer->Read(data, size);

    memcpy((char*)&(*_support_version.begin()), data, size);
    alloter->PoolFree(data, size);

    return true;
}

uint32_t VersionNegotiationPacket::EncodeSize() {
    return sizeof(VersionNegotiationPacket) + _support_version.size() * sizeof(uint32_t);
}

bool VersionNegotiationPacket::AddFrame(std::shared_ptr<Frame> frame) {
    // do nothing
    return true;
}

}
