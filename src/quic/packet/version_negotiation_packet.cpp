#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {
namespace quic {

VersionNegotiationPacket::VersionNegotiationPacket() {
    header_.GetLongHeaderFlag().SetPacketType(PT_NEGOTIATION);
    header_.SetVersion(0);
}

VersionNegotiationPacket::VersionNegotiationPacket(uint8_t flag):
    header_(flag) {
    header_.GetLongHeaderFlag().SetPacketType(PT_NEGOTIATION);
    header_.SetVersion(0);

}

VersionNegotiationPacket::~VersionNegotiationPacket() {
    
}

bool VersionNegotiationPacket::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (!header_.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    for (size_t i = 0; i < support_version_.size(); i++) {
        cur_pos = common::FixedEncodeUint32(cur_pos, end, support_version_[i]);
    }
    
    packet_src_data_ = std::move(common::BufferSpan(start_pos, cur_pos));
    buffer->MoveWritePt(cur_pos - span.GetStart());
    return true;
}

bool VersionNegotiationPacket::DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer) {
    if (!header_.DecodeHeader(buffer)) {
        common::LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* cur_pos = span.GetStart();

    uint32_t version_count = (span.GetEnd() - span.GetStart()) / sizeof(uint32_t);
    support_version_.resize(version_count);
    for (size_t i = 0; i < version_count; i++) {
        cur_pos = common::FixedDecodeUint32(cur_pos, span.GetEnd(), support_version_[i]);
    }

    packet_src_data_ = std::move(common::BufferSpan(span.GetStart(), cur_pos));
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

void VersionNegotiationPacket::SetSupportVersion(std::vector<uint32_t> versions) {
    support_version_.insert(support_version_.end(), versions.begin(), versions.end());
}

void VersionNegotiationPacket::AddSupportVersion(uint32_t version) {
    support_version_.push_back(version);
}

}
}