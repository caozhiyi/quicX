#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>
#include "quic/frame/type.h"
#include "quic/packet/type.h"
#include "common/buffer/buffer_span.h"
#include "quic/frame/frame_interface.h"
#include "quic/packet/header/header_interface.h"
#include "quic/crypto/cryptographer_interface.h"
#include "common/buffer/buffer_read_interface.h"
#include "common/buffer/buffer_write_interface.h"

namespace quicx {

class IPacket {
public:
    IPacket(): _frame_type_bit(0), _packet_number(0) {}
    virtual ~IPacket() {}

    virtual uint16_t GetCryptoLevel() const = 0;
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer) = 0;
    virtual bool DecodeWithoutCrypto(std::shared_ptr<IBufferRead> buffer) = 0;
    virtual bool DecodeWithCrypto(std::shared_ptr<IBuffer> buffer) = 0;

    virtual IHeader* GetHeader() = 0;
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames();

    uint32_t GetPacketNumOffset() { return 0; };
    uint64_t GetPacketNumber() { return _packet_number; }
    void SetPacketNumber(uint64_t num) { _packet_number = num; }

    BufferSpan& GetSrcBuffer() { return _packet_src_data; }

    void AddFrameTypeBit(FrameTypeBit bit) { _frame_type_bit |= bit; }
    uint32_t GetFrameTypeBit() { return _frame_type_bit; }

    void SetCryptographer(std::shared_ptr<ICryptographer> crypto_grapher) { _crypto_grapher = crypto_grapher; }
    
protected:
    uint32_t _frame_type_bit;
    uint64_t _packet_number; /*encryption protection*/
    BufferSpan _packet_src_data;

    std::shared_ptr<ICryptographer> _crypto_grapher;
};

}

#endif