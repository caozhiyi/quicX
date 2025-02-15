
#ifndef QUIC_PACKET_RTT_1_PACKET
#define QUIC_PACKET_RTT_1_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/common/constants.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/header/short_header.h"

namespace quicx {
namespace quic {

class Rtt1Packet:
    public IPacket {
public:
    Rtt1Packet();
    Rtt1Packet(uint8_t flag);
    virtual ~Rtt1Packet();

    virtual uint16_t GetCryptoLevel() const { return PakcetCryptoLevel::kApplicationCryptoLevel; }
    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer);
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer);

    virtual IHeader* GetHeader() { return &header_; }
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return frames_list_; }

    void SetPayload(common::BufferSpan payload);
    common::BufferSpan GetPayload() { return payload_; }
    uint32_t GetPayloadLength() { return payload_.GetEnd() - payload_.GetStart(); }

protected:
    ShortHeader header_;
    common::BufferSpan payload_;

    uint32_t payload_offset_;
    uint64_t largest_pn_;
    std::vector<std::shared_ptr<IFrame>> frames_list_;
};

}
}

#endif