
#ifndef QUIC_PACKET_RTT_0_PACKET
#define QUIC_PACKET_RTT_0_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

class Rtt0Packet:
    public IPacket {
public:
    Rtt0Packet();
    Rtt0Packet(uint8_t flag);
    virtual ~Rtt0Packet();

    virtual uint16_t GetCryptoLevel() const { return PCL_ELAY_DATA; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher = nullptr);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual bool Decode(std::shared_ptr<ICryptographer> crypto_grapher);

    virtual IHeader* GetHeader() { return &_header; }
    virtual uint32_t GetPacketNumOffset() { return 0; }
    virtual bool AddFrame(std::shared_ptr<IFrame> frame);
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { }

    virtual PacketType GetPacketType() { return PT_0RTT; }

private:
    LongHeader _header;
    uint32_t _payload_length;
    uint32_t _packet_number;
    char* _payload;
};

}

#endif