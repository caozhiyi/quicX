#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>
#include "quic/packet/type.h"
#include "common/buffer/buffer_span.h"
#include "quic/frame/frame_interface.h"
#include "quic/packet/header/header_interface.h"
#include "common/buffer/buffer_read_interface.h"
#include "common/buffer/buffer_write_interface.h"

namespace quicx {

class IPacket {
public:
    IPacket() {}
    virtual ~IPacket() {}

    virtual uint16_t GetCryptoLevel() const = 0;
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer) = 0;
    virtual bool DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer) { return true; };
    virtual bool DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer) { return true; };

    virtual IHeader* GetHeader() = 0;
    virtual uint32_t GetPayloadLength() { return 0; };
    virtual bool AddFrame(std::shared_ptr<IFrame> frame) {  return true; }
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return _no_use; }

    virtual uint32_t GetPacketNumOffset() { return 0; };
    virtual uint64_t GetPacketNumber() { return _packet_number; }
    virtual void SetPacketNumber(uint64_t num) { _packet_number = num; }
protected:
    uint64_t _packet_number; /*encryption protection*/

    std::vector<std::shared_ptr<IFrame>> _no_use;
};

}

#endif