#ifndef QUIC_PACKET_HEADER_LONG_PACKET
#define QUIC_PACKET_HEADER_LONG_PACKET

#include <memory>
#include "quic/common/constants.h"
#include "quic/packet/header/if_header.h"
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

class LongHeader: public IHeader {
public:
    LongHeader();
    LongHeader(uint8_t flag);
    virtual ~LongHeader();

    virtual bool EncodeHeader(std::shared_ptr<common::IBuffer> buffer) override;
    virtual bool DecodeHeader(std::shared_ptr<common::IBuffer> buffer, bool with_flag = false) override;
    virtual uint32_t EncodeHeaderSize() override;

    void SetVersion(uint32_t version) { version_ = version; }
    uint32_t GetVersion() const { return version_; }

    // Override to correctly identify Version Negotiation packets.
    // RFC 9000 §17.2.1: Version Negotiation packets have version=0 and their
    // flag bits (including packet_type) can be arbitrary values, so we must
    // check the version field first.
    // The in-memory packet_type_ bitfield is always normalized to the
    // canonical v1 representation by EncodeHeader/DecodeHeader, even when the
    // wire format uses QUIC v2 (RFC 9369 §3.2) remapping, so the base-class
    // GetPacketType() can be used here directly.
    PacketType GetPacketType() override {
        if (version_ == 0) {
            return PacketType::kNegotiationPacketType;
        }
        return HeaderFlag::GetPacketType();
    }

    void SetDestinationConnectionId(const uint8_t* id, uint8_t len) override;
    uint8_t GetDestinationConnectionIdLength() override { return destination_connection_id_length_; }
    const uint8_t* GetDestinationConnectionId() { return destination_connection_id_; }

    void SetSourceConnectionId(const uint8_t* id, uint8_t len);
    uint8_t GetSourceConnectionIdLength() { return source_connection_id_length_; }
    const uint8_t* GetSourceConnectionId() { return source_connection_id_; }

protected:
    uint32_t version_;

    uint8_t destination_connection_id_length_;
    uint8_t destination_connection_id_[kMaxConnectionLength];

    uint8_t source_connection_id_length_;
    uint8_t source_connection_id_[kMaxConnectionLength];
};

}  // namespace quic
}  // namespace quicx

#endif