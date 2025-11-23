#ifndef QUIC_STREAM_CRYPTO_STREAM
#define QUIC_STREAM_CRYPTO_STREAM

#include <unordered_map>
#include "quic/crypto/tls/type.h"
#include "quic/stream/if_stream.h"
#include "common/buffer/multi_block_buffer.h"

namespace quicx {
namespace quic {

class CryptoStream:
    public IStream {
public:
    CryptoStream(std::shared_ptr<common::BlockMemoryPool> alloter,
        std::shared_ptr<common::IEventLoop> loop,
        std::function<void(std::shared_ptr<IStream>)> active_send_cb,
        std::function<void(uint64_t stream_id)> stream_close_cb,
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb);
    virtual ~CryptoStream();
    virtual StreamDirection GetDirection() { return StreamDirection::kBidi; }

    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);

    virtual uint64_t GetStreamID() { return stream_id_; }

    // reset the stream
    virtual void Reset(uint32_t error);

    virtual void Close();

    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame);

    virtual int32_t Send(uint8_t* data, uint32_t len, uint8_t encryption_level);
    virtual int32_t Send(uint8_t* data, uint32_t len);
    virtual int32_t Send(std::shared_ptr<IBufferRead> data);

    virtual uint8_t GetWaitSendEncryptionLevel();

    virtual void SetStreamReadCallBack(stream_read_callback cb) { recv_cb_ = cb; }

protected:
    void OnCryptoFrame(std::shared_ptr<IFrame> frame);

private:
    std::shared_ptr<common::BlockMemoryPool> alloter_;
    std::shared_ptr<common::MultiBlockBuffer> buffer_;

    // in order next data offset
    uint64_t except_offset_;
    std::unordered_map<uint64_t, std::shared_ptr<IFrame>> out_order_frame_;

    // local data send offset
    uint64_t send_offset_;
    std::shared_ptr<common::MultiBlockBuffer> send_buffers_[kNumEncryptionLevels];
    stream_read_callback recv_cb_;
};

}
}

#endif