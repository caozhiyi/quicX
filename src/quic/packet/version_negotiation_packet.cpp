#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {

VersionNegotiationPacket::VersionNegotiationPacket() {
    _header.GetLongHeaderFlag().SetPacketType(PT_NEGOTIATION);
    _header.SetVersion(0);
}

VersionNegotiationPacket::VersionNegotiationPacket(uint8_t flag):
    _header(flag) {
    _header.GetLongHeaderFlag().SetPacketType(PT_NEGOTIATION);
    _header.SetVersion(0);

}

VersionNegotiationPacket::~VersionNegotiationPacket() {
    
}

bool VersionNegotiationPacket::Encode(std::shared_ptr<IBufferWrite> buffer) {
    if (!_header.EncodeHeader(buffer)) {
        LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;

    for (size_t i = 0; i < _support_version.size(); i++) {
        cur_pos = FixedEncodeUint32(cur_pos, _support_version[i]);
    }
    
    _packet_src_data = std::move(BufferSpan(start_pos, cur_pos));
    buffer->MoveWritePt(cur_pos - span.GetStart());
    return true;
}

bool VersionNegotiationPacket::DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    uint32_t version_count = (span.GetEnd() - start_pos) / sizeof(uint32_t);
    _support_version.resize(version_count);
    for (size_t i = 0; i < version_count; i++) {
        cur_pos = FixedDecodeUint32(cur_pos, end, _support_version[i]);
    }

    _packet_src_data = std::move(BufferSpan(start_pos, cur_pos));
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

bool VersionNegotiationPacket::DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer) {
    return true; // do nothing
}

uint32_t VersionNegotiationPacket::EncodeSize() {
    return 0;
}

bool VersionNegotiationPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    // do nothing
    return true;
}

void VersionNegotiationPacket::SetSupportVersion(std::vector<uint32_t> versions) {
    _support_version.insert(_support_version.end(), versions.begin(), versions.end());
}

void VersionNegotiationPacket::AddSupportVersion(uint32_t version) {
    _support_version.push_back(version);
}

}
