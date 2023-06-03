
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
    virtual ~Rtt1Packet();

    virtual uint16_t GetCryptoLevel() const { return PCL_APPLICATION; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer);
    virtual bool DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }
    virtual bool AddFrame(std::shared_ptr<IFrame> frame);
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return _frame_list; }

    void SetPayload(BufferSpan payload);
    BufferSpan GetPayload() { return _palyload; }
    uint32_t GetPayloadLength() { return _palyload.GetEnd() - _palyload.GetStart(); }

protected:
    ShortHeader _header;
    uint64_t _packet_num;
    BufferSpan _palyload;

    uint32_t _payload_offset;
    uint32_t _packet_num_offset;
    std::vector<std::shared_ptr<IFrame>> _frame_list;
};

}

#endif