
#ifndef QUIC_PACKET_RTT_0_PACKET
#define QUIC_PACKET_RTT_0_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/header/long_header.h"

namespace quicx {
namespace quic {

/*
0-RTT Packet {
    Header Form (1) = 1,
    Fixed Bit (1) = 1,
    Long Packet Type (2) = 1,
    Reserved Bits (2),
    Packet Number Length (2),
    Version (32),
    Destination Connection ID Length (8),
    Destination Connection ID (0..160),
    Source Connection ID Length (8),
    Source Connection ID (0..160),
    Length (i),
    Packet Number (8..32),
    Packet Payload (8..),
}
*/

class Rtt0Packet:
    public IPacket {
public:
    Rtt0Packet();
    Rtt0Packet(uint8_t flag);
    virtual ~Rtt0Packet();

    virtual uint16_t GetCryptoLevel() const { return PakcetCryptoLevel::kEarlyDataCryptoLevel; }
    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBuffer> buffer, bool with_flag = false);
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer);

    virtual IHeader* GetHeader() { return &header_; }
    virtual uint32_t GetPacketNumOffset() { return packet_num_offset_; }
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return frames_list_; }

    void SetPayload(const common::SharedBufferSpan& payload);
    common::SharedBufferSpan GetPayload() { return payload_; }
    uint32_t GetLength() { return length_; }

private:
    LongHeader header_;
    uint32_t length_;
    common::SharedBufferSpan payload_;

    uint32_t payload_offset_;
    uint32_t packet_num_offset_;
    std::vector<std::shared_ptr<IFrame>> frames_list_;
};

}
}

#endif