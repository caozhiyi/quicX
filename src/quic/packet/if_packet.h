#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>
#include <memory>
#include <vector>
#include "quic/frame/type.h"
#include "quic/frame/if_frame.h"
#include "common/buffer/if_buffer.h"
#include "quic/packet/header/if_header.h"
#include "quic/crypto/if_cryptographer.h"
#include "common/buffer/shared_buffer_span.h"

namespace quicx {
namespace quic {

class IPacket {
public:
    IPacket(): frame_type_bit_(0), packet_number_(0) {}
    virtual ~IPacket() {}

    virtual uint16_t GetCryptoLevel() const = 0;
    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer) = 0;
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBuffer> buffer, bool with_flag = false) = 0;
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer) = 0;

    virtual IHeader* GetHeader() = 0;
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames();

    uint32_t GetPacketNumOffset() { return 0; };
    uint64_t GetPacketNumber() { return packet_number_; }
    void SetPacketNumber(uint64_t num) { packet_number_ = num; }

    common::SharedBufferSpan& GetSrcBuffer() { return packet_src_data_; }

    void AddFrameTypeBit(FrameTypeBit bit) { frame_type_bit_ |= bit; }
    uint32_t GetFrameTypeBit() { return frame_type_bit_; }

    virtual void SetPayload(const common::SharedBufferSpan& payload) {}

    void SetCryptographer(std::shared_ptr<ICryptographer> crypto_grapher) { crypto_grapher_ = crypto_grapher; }
    
protected:
    uint32_t frame_type_bit_;
    uint64_t packet_number_; /*encryption protection*/
    common::SharedBufferSpan packet_src_data_;

    std::shared_ptr<ICryptographer> crypto_grapher_;
};

}
}

#endif