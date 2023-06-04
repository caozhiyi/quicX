
#ifndef QUIC_PACKET_VERSION_NEGOTIATION_PACKET
#define QUIC_PACKET_VERSION_NEGOTIATION_PACKET

#include <memory>
#include <vector>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

class VersionNegotiationPacket:
    public IPacket {
public:
    VersionNegotiationPacket();
    VersionNegotiationPacket(uint8_t flag);
    virtual ~VersionNegotiationPacket();

    virtual uint16_t GetCryptoLevel() const { return PCL_UNCRYPTO; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer);
    virtual bool DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }
    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    void SetSupportVersion(std::vector<uint32_t> versions);
    void AddSupportVersion(uint32_t version);
    const std::vector<uint32_t>& GetSupportVersion() { return _support_version; }

private:
    LongHeader _header;
    std::vector<uint32_t> _support_version;
};

}

#endif