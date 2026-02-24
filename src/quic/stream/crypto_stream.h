#ifndef QUIC_STREAM_CRYPTO_STREAM
#define QUIC_STREAM_CRYPTO_STREAM

#include <unordered_map>
#include "common/buffer/multi_block_buffer.h"
#include "quic/crypto/tls/type.h"
#include "quic/stream/if_stream.h"

namespace quicx {
namespace quic {

class CryptoStream: public IStream {
public:
    CryptoStream(std::shared_ptr<common::IEventLoop> loop, std::function<void(std::shared_ptr<IStream>)> active_send_cb,
        std::function<void(uint64_t stream_id)> stream_close_cb,
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb);
    virtual ~CryptoStream();
    virtual StreamDirection GetDirection() override { return StreamDirection::kBidi; }

    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor, EncryptionLevel level = kApplication) override;

    virtual uint64_t GetStreamID() override { return stream_id_; }

    // reset the stream
    virtual void Reset(uint32_t error) override;

    // Reset state for Retry (clears Initial level buffers and offsets)
    void ResetForRetry();

    virtual void Close();

    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame) override;

    virtual int32_t Send(uint8_t* data, uint32_t len, uint8_t encryption_level);
    virtual int32_t Send(uint8_t* data, uint32_t len);
    virtual int32_t Send(std::shared_ptr<IBufferRead> buffer);

    virtual uint8_t GetWaitSendEncryptionLevel();

    using crypto_stream_read_callback =
        std::function<void(std::shared_ptr<IBufferRead> buffer, int32_t err, uint16_t encryption_level)>;
    virtual void SetCryptoStreamReadCallBack(crypto_stream_read_callback cb) { recv_cb_ = cb; }

protected:
    void OnCryptoFrame(std::shared_ptr<IFrame> frame);

private:
    // read buffers for each encryption level
    std::shared_ptr<common::MultiBlockBuffer> read_buffers_[kNumEncryptionLevels];

    // in order next data offset for each encryption level
    uint64_t next_read_offset_[kNumEncryptionLevels];
    std::unordered_map<uint64_t, std::shared_ptr<IFrame>> out_order_frame_[kNumEncryptionLevels];

    // local data send offset for each encryption level
    uint64_t send_offset_[kNumEncryptionLevels];
    std::shared_ptr<common::MultiBlockBuffer> send_buffers_[kNumEncryptionLevels];

    crypto_stream_read_callback recv_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif