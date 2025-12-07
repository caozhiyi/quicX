#include "common/buffer/buffer_decode_wrapper.h"
#include "common/decode/decode.h"
#include "common/log/log.h"

#include "quic/common/constants.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {
namespace quic {

VersionNegotiationPacket::VersionNegotiationPacket() {
    header_.GetLongHeaderFlag().SetPacketType(PacketType::kNegotiationPacketType);
    header_.SetVersion(0);
}

VersionNegotiationPacket::VersionNegotiationPacket(uint8_t flag):
    header_(flag) {
    header_.GetLongHeaderFlag().SetPacketType(PacketType::kNegotiationPacketType);
    header_.SetVersion(0);
}

VersionNegotiationPacket::~VersionNegotiationPacket() {}

bool VersionNegotiationPacket::Encode(std::shared_ptr<common::IBuffer> buffer) {
    if (!header_.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWritableSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    for (size_t i = 0; i < support_version_.size(); i++) {
        cur_pos = common::FixedEncodeUint32(cur_pos, end, support_version_[i]);
    }

    buffer->MoveWritePt(cur_pos - span.GetStart());
    return true;
}

bool VersionNegotiationPacket::DecodeWithoutCrypto(std::shared_ptr<common::IBuffer> buffer, bool with_flag) {
    // Version Negotiation packet has special decoding logic
    // RFC 9000 Section 17.2.1: Unused bits (including fixed bit position) can be arbitrary values,
    // so we don't check the fixed bit like other long header packets

    if (with_flag) {
        // Flag already decoded by caller in packet_decode.cpp, skip it here
        // We do NOT call header_.DecodeHeader() because it would check fixed bit
    }

    // IMPORTANT: Set packet type explicitly to kNegotiationPacketType
    // Otherwise GetPacketType() will return the wrong type based on flag bits
    header_.GetLongHeaderFlag().SetPacketType(PacketType::kNegotiationPacketType);

    common::BufferDecodeWrapper wrapper(buffer);

    // Decode Version (must be 0 for Version Negotiation)
    uint32_t version;
    wrapper.DecodeFixedUint32(version);
    if (version != 0) {
        common::LOG_ERROR("version negotiation packet must have version 0, got: 0x%08x", version);
        return false;
    }
    header_.SetVersion(version);

    // Decode DCID Length
    uint8_t dcid_len;
    wrapper.DecodeFixedUint8(dcid_len);
    if (dcid_len > kMaxConnectionLength) {
        common::LOG_ERROR("DCID length too large: %u", dcid_len);
        return false;
    }

    // Decode DCID
    uint8_t dcid[kMaxConnectionLength];
    if (dcid_len > 0) {
        auto cid = (uint8_t*)dcid;
        wrapper.DecodeBytes(cid, dcid_len);
    }
    header_.SetDestinationConnectionId(dcid, dcid_len);

    // Decode SCID Length
    uint8_t scid_len;
    wrapper.DecodeFixedUint8(scid_len);
    if (scid_len > kMaxConnectionLength) {
        common::LOG_ERROR("SCID length too large: %u", scid_len);
        return false;
    }

    // Decode SCID
    uint8_t scid[kMaxConnectionLength];
    if (scid_len > 0) {
        auto cid = (uint8_t*)scid;
        wrapper.DecodeBytes(cid, scid_len);
    }
    header_.SetSourceConnectionId(scid, scid_len);

    // IMPORTANT: Move buffer read pointer to reflect what wrapper has consumed
    // wrapper.GetReadLength() tells us how many bytes wrapper has read
    uint32_t header_bytes_read = wrapper.GetReadLength();
    buffer->MoveReadPt(header_bytes_read);

    // Now decode supported versions list from remaining buffer
    auto span = buffer->GetReadableSpan();
    auto shared_span = buffer->GetSharedReadableSpan();
    if (!shared_span.Valid()) {
        common::LOG_ERROR("readable span is invalid");
        return false;
    }
    auto chunk = shared_span.GetChunk();
    uint8_t* cur_pos = span.GetStart();

    uint32_t version_count = (span.GetEnd() - span.GetStart()) / sizeof(uint32_t);
    support_version_.resize(version_count);

    common::LOG_INFO("Version Negotiation packet: server supports %u versions", version_count);
    for (size_t i = 0; i < version_count; i++) {
        cur_pos = common::FixedDecodeUint32(cur_pos, span.GetEnd(), support_version_[i]);
        common::LOG_INFO("  Version[%zu]: 0x%08x", i, support_version_[i]);
    }

    if (version_count == 0) {
        common::LOG_WARN("Version Negotiation packet has no supported versions");
    }

    packet_src_data_ = common::SharedBufferSpan(chunk, span.GetStart(), cur_pos);
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

void VersionNegotiationPacket::SetSupportVersion(std::vector<uint32_t> versions) {
    support_version_.insert(support_version_.end(), versions.begin(), versions.end());
}

void VersionNegotiationPacket::AddSupportVersion(uint32_t version) {
    support_version_.push_back(version);
}

}  // namespace quic
}  // namespace quicx