#ifndef QUIC_STREAM_CRYPTO_STREAM
#define QUIC_STREAM_CRYPTO_STREAM

#include <unordered_map>
#include "quic/crypto/tls/type.h"
#include "common/buffer/buffer_chains.h"
#include "quic/stream/send_stream_interface.h"
#include "quic/stream/recv_stream_interface.h"

namespace quicx {

class CryptoStream:
    public virtual ISendStream,
    public virtual IRecvStream {
public:
    CryptoStream(std::shared_ptr<BlockMemoryPool> alloter);
    virtual ~CryptoStream();

    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);

    virtual void Reset(uint64_t err);

    virtual void Close(uint64_t err = 0);

    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame);

    virtual int32_t Send(uint8_t* data, uint32_t len, uint8_t encryption_level);
    virtual int32_t Send(uint8_t* data, uint32_t len);

    virtual uint8_t GetWaitSendEncryptionLevel();

protected:
    void OnCryptoFrame(std::shared_ptr<IFrame> frame);

private:
    std::shared_ptr<BlockMemoryPool> _alloter;

    uint64_t _except_offset;
    std::shared_ptr<IBufferChains> _recv_buffer;
    std::unordered_map<uint64_t, std::shared_ptr<IFrame>> _out_order_frame;

    uint64_t _send_offset;
    std::shared_ptr<IBufferChains> _send_buffers[NUM_ENCRYPTION_LEVELS];
};

}

#endif