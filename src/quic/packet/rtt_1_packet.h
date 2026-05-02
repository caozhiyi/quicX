
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

    virtual uint16_t GetCryptoLevel() const { return PacketCryptoLevel::kApplicationCryptoLevel; }
    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBuffer> buffer, bool with_flag = false);
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer);

    virtual IHeader* GetHeader() { return &header_; }
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return frames_list_; }

    void SetPayload(const common::SharedBufferSpan& payload);
    common::SharedBufferSpan GetPayload() { return payload_; }
    uint32_t GetPayloadLength() { return payload_.GetEnd() - payload_.GetStart(); }

    // RFC 9001 §6: Set the expected key phase for Key Update detection
    void SetExpectedKeyPhase(uint8_t key_phase) { expected_key_phase_ = key_phase; }

    // RFC 9001 §6: Retry only the payload decryption after Key Update
    // Called when initial decrypt failed due to key phase change, and the connection
    // has rotated the read keys. Skips header decryption (already done).
    bool RetryPayloadDecrypt();

protected:
    ShortHeader header_;
    common::SharedBufferSpan payload_;

    uint32_t payload_offset_;
    uint8_t expected_key_phase_ = 0;  // RFC 9001 §6: expected key phase from connection
    // Saved state for retry after Key Update
    uint8_t* saved_payload_start_ = nullptr;
    uint8_t* saved_ad_start_ = nullptr;
    uint8_t* saved_payload_end_ = nullptr;
    uint64_t saved_truncated_pn_ = 0;
    uint8_t saved_header_len_ = 0;
    std::vector<std::shared_ptr<IFrame>> frames_list_;
};

}
}

#endif