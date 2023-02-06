#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>
#include "quic/packet/type.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {

class IFrame;
class IHeader;

class IPacket {
public:
    IPacket() {}
    IPacket(std::shared_ptr<IHeader> header): _header(header) {}
    virtual ~IPacket() {}

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer) = 0;
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer) = 0;
    virtual uint32_t EncodeSize() = 0;

    virtual PacketType GetPacketType() = 0;
    virtual uint64_t GetPacketNumber() { return _packet_number; }
    virtual void SetPacketNumber(uint64_t num) { _packet_number = num; }

    std::shared_ptr<IHeader> GetHeader() { return _header; }

protected:
    std::shared_ptr<IHeader> _header;
    uint64_t _packet_number; /*encryption protection*/
    std::pair<const uint8_t*, const uint8_t*> _src_data;
};

}

#endif