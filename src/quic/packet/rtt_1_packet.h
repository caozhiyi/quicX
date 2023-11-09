
#ifndef QUIC_PACKET_RTT_1_PACKET
#define QUIC_PACKET_RTT_1_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/common/constants.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/short_header.h"

namespace quicx {

class Rtt1Packet:
    public IPacket {
public:
    Rtt1Packet();
    Rtt1Packet(uint8_t flag);
    virtual ~Rtt1Packet();

    virtual uint16_t GetCryptoLevel() const { return PCL_APPLICATION; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<IBufferRead> buffer);
    virtual bool DecodeWithCrypto(std::shared_ptr<IBuffer> buffer);

    virtual IHeader* GetHeader() { return &_header; }
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return _frames_list; }

    void SetPayload(BufferSpan payload);
    BufferSpan GetPayload() { return _payload; }
    uint32_t GetPayloadLength() { return _payload.GetEnd() - _payload.GetStart(); }

protected:
    ShortHeader _header;
    BufferSpan _payload;

    uint32_t _payload_offset;
    uint64_t _largest_pn;
    std::vector<std::shared_ptr<IFrame>> _frames_list;
};

}

#endif