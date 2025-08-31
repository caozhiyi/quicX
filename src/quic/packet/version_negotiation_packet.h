#ifndef QUIC_PACKET_VERSION_NEGOTIATION_PACKET
#define QUIC_PACKET_VERSION_NEGOTIATION_PACKET

#include <memory>
#include <vector>
#include "quic/packet/type.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/header/long_header.h"

namespace quicx {
namespace quic {

class VersionNegotiationPacket:
    public IPacket {
public:
    VersionNegotiationPacket();
    VersionNegotiationPacket(uint8_t flag);
    virtual ~VersionNegotiationPacket();

    virtual uint16_t GetCryptoLevel() const { return PakcetCryptoLevel::kUnknownCryptoLevel; }
    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer, bool with_flag = false);
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer) { return true; }

    virtual IHeader* GetHeader() { return &header_; }

    void SetSupportVersion(std::vector<uint32_t> versions);
    void AddSupportVersion(uint32_t version);
    const std::vector<uint32_t>& GetSupportVersion() { return support_version_; }

private:
    LongHeader header_;
    std::vector<uint32_t> support_version_;
};

}
}

#endif