#include <cstring>
#include <memory>

#include "common/alloter/if_alloter.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"
#include "quic/common/constants.h"
#include "quic/packet/header/long_header.h"

namespace quicx {
namespace quic {

LongHeader::LongHeader():
    IHeader(PacketHeaderType::kLongHeader),
    version_(0),
    destination_connection_id_length_(0),
    source_connection_id_length_(0) {}

LongHeader::LongHeader(uint8_t flag):
    IHeader(flag),
    version_(0),
    destination_connection_id_length_(0),
    source_connection_id_length_(0) {}

LongHeader::~LongHeader() {}

bool LongHeader::EncodeHeader(std::shared_ptr<common::IBuffer> buffer) {
    // RFC 9369 §3.2: Long Header Packet Type wire bits depend on QUIC version.
    // Concrete packet type constructors set packet_type_ to the v1 enum value
    // (Initial=0, 0-RTT=1, Handshake=2, Retry=3). Translate it to the
    // version-specific wire representation just for writing the flag byte
    // (which also becomes part of the AEAD AD), then restore the v1 enum in
    // memory so subsequent encode calls (e.g. retransmissions) stay correct.
    PacketType logical_type = HeaderFlag::GetPacketType();
    uint8_t wire_bits = MapPacketTypeToWire(logical_type, version_);
    uint8_t v1_bits = static_cast<uint8_t>(logical_type) & 0x03;
    GetLongHeaderFlag().SetPacketType(wire_bits);

    bool flag_ok = HeaderFlag::EncodeFlag(buffer);
    // Restore the in-memory representation regardless of success.
    GetLongHeaderFlag().SetPacketType(v1_bits);
    if (!flag_ok) {
        return false;
    }

    uint32_t need_size = EncodeHeaderSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);

    // encode version
    wrapper.EncodeFixedUint32(version_);
    wrapper.EncodeFixedUint8(destination_connection_id_length_);
    if (destination_connection_id_length_ > 0) {
        wrapper.EncodeBytes(destination_connection_id_, destination_connection_id_length_);
    }
    wrapper.EncodeFixedUint8(source_connection_id_length_);
    if (source_connection_id_length_ > 0) {
        wrapper.EncodeBytes(source_connection_id_, source_connection_id_length_);
    }

    auto data_span = wrapper.GetDataSpan();
    // the header src include header flag
    header_src_data_ = common::SharedBufferSpan(buffer->GetChunk(), data_span.GetStart() - 1, data_span.GetEnd());

    return true;
}

bool LongHeader::DecodeHeader(std::shared_ptr<common::IBuffer> buffer, bool with_flag) {
    if (with_flag) {
        if (!HeaderFlag::DecodeFlag(buffer)) {
            return false;
        }
    }

    // check flag fixed bit
    if (!HeaderFlag::GetLongHeaderFlag().fix_bit_) {
        common::LOG_ERROR("quic fixed bit is not set");
        return false;
    }

    // Capture the *wire* flag byte before we possibly normalize packet_type_
    // below: the byte in `buffer` at `header_start` must stay as-is because it
    // participates in AEAD AD (and header protection already operated on it).
    uint8_t wire_flag_byte = flag_.header_flag_;

    common::BufferDecodeWrapper wrapper(buffer);
    // decode version
    wrapper.DecodeFixedUint32(version_);

    // RFC 9369 §3.2: for QUIC v2 the two packet-type wire bits need to be
    // translated to the canonical v1 enum representation used by the rest of
    // the stack. We mutate `flag_.long_header_flag_.packet_type_` in memory
    // only; the wire byte in the buffer is preserved (see below).
    if (version_ != 0) {
        uint8_t wire_bits = GetLongHeaderFlag().GetPacketType();
        PacketType logical = MapWireToPacketType(wire_bits, version_);
        uint8_t v1_bits = static_cast<uint8_t>(logical) & 0x03;
        GetLongHeaderFlag().SetPacketType(v1_bits);
    }

    // decode dcid
    wrapper.DecodeFixedUint8(destination_connection_id_length_);
    if (destination_connection_id_length_ > kMaxConnectionLength) {
        common::LOG_ERROR("destination connection id length exceeds maximum. length:%d, max:%d",
            destination_connection_id_length_, kMaxConnectionLength);
        return false;
    }
    if (destination_connection_id_length_ > 0) {
        auto cid = (uint8_t*)destination_connection_id_;
        wrapper.DecodeBytes(cid, destination_connection_id_length_);
    }

    // decode scid
    wrapper.DecodeFixedUint8(source_connection_id_length_);
    if (source_connection_id_length_ > kMaxConnectionLength) {
        common::LOG_ERROR("source connection id length exceeds maximum. length:%d, max:%d",
            source_connection_id_length_, kMaxConnectionLength);
        return false;
    }
    if (source_connection_id_length_ > 0) {
        auto cid = (uint8_t*)source_connection_id_;
        wrapper.DecodeBytes(cid, source_connection_id_length_);
    }

    auto data_span = wrapper.GetDataSpan();
    // the header src must include header flag for AEAD AD construction.
    // We must restore the ORIGINAL wire flag byte here (not the normalized
    // in-memory value) because AEAD AD is computed over the wire bytes.
    uint8_t* header_start = data_span.GetStart() - 1;
    *header_start = wire_flag_byte;

    header_src_data_ = common::SharedBufferSpan(buffer->GetChunk(), header_start, data_span.GetEnd());
    
    return true;
}

uint32_t LongHeader::EncodeHeaderSize() {
    return sizeof(LongHeader);
}

void LongHeader::SetDestinationConnectionId(const uint8_t* id, uint8_t len) {
    if (len > kMaxConnectionLength) {
        len = kMaxConnectionLength;
    }
    destination_connection_id_length_ = len;
    if (id != nullptr) {
        memcpy(destination_connection_id_, id, len);
    }
}

void LongHeader::SetSourceConnectionId(const uint8_t* id, uint8_t len) {
    if (len > kMaxConnectionLength) {
        len = kMaxConnectionLength;
    }
    source_connection_id_length_ = len;
    if (id != nullptr) {
        memcpy(source_connection_id_, id, len);
    }
}

}  // namespace quic
}  // namespace quicx