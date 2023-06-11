
#ifndef QUIC_PACKET_RTT_0_PACKET
#define QUIC_PACKET_RTT_0_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

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

    virtual uint16_t GetCryptoLevel() const { return PCL_ELAY_DATA; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher = nullptr);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual bool Decode(std::shared_ptr<IBuffer> buffer, std::shared_ptr<ICryptographer> crypto_grapher);

    virtual IHeader* GetHeader() { return &_header; }
    virtual uint32_t GetPacketNumOffset() { return _packet_num_offset; }
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return _frames_list; }

    void SetPayload(BufferSpan payload);
    BufferSpan GetPayload() { return _payload; }
    uint32_t GetLength() { return _length; }

private:
    LongHeader _header;
    uint32_t _length;
    BufferSpan _payload;

    uint32_t _payload_offset;
    uint32_t _packet_num_offset;
    std::vector<std::shared_ptr<IFrame>> _frames_list;
};

}

#endif