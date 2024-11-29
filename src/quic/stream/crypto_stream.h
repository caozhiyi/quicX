#ifndef QUIC_STREAM_CRYPTO_STREAM
#define QUIC_STREAM_CRYPTO_STREAM

#include <unordered_map>
#include "quic/crypto/tls/type.h"
#include "quic/stream/if_send_stream.h"
#include "quic/stream/if_recv_stream.h"
#include "common/buffer/buffer_chains.h"

namespace quicx {
namespace quic {

class CryptoStream:
    public virtual ISendStream,
    public virtual IRecvStream {
public:
    CryptoStream(std::shared_ptr<common::BlockMemoryPool> alloter);
    virtual ~CryptoStream();

    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);

    virtual void Reset(uint64_t err);

    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame);

    virtual int32_t Send(uint8_t* data, uint32_t len, uint8_t encryption_level);
    virtual int32_t Send(uint8_t* data, uint32_t len);

    virtual uint8_t GetWaitSendEncryptionLevel();

protected:
    void OnCryptoFrame(std::shared_ptr<IFrame> frame);

private:
    std::shared_ptr<common::BlockMemoryPool> alloter_;

    // in order next data offset
    uint64_t except_offset_;
    // only use one recv buffer, since write data to ssl library do not need crypto level
    std::shared_ptr<common::IBufferChains> recv_buffer_;
    std::unordered_map<uint64_t, std::shared_ptr<IFrame>> out_order_frame_;

    // local data send offset
    uint64_t send_offset_;
    std::shared_ptr<common::IBufferChains> send_buffers_[NUM_ENCRYPTION_LEVELS];
};

}
}

#endif