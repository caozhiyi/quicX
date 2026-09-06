#ifndef QUIC_STREAM_CRYPTO_STREAM
#define QUIC_STREAM_CRYPTO_STREAM

#include <cstdint>
#include <map>

#include "common/buffer/multi_block_buffer.h"

#include "quic/config.h"
#include "quic/crypto/tls/type.h"
#include "quic/stream/if_stream.h"

namespace quicx {
namespace quic {

class CryptoStream: public IStream {
public:
    CryptoStream(std::weak_ptr<common::IEventLoop> loop, std::function<void(std::shared_ptr<IStream>)> active_send_cb,
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

    // Non-destructive query: does this crypto stream currently have
    // unsent plaintext bytes queued at |level|? Used by the burst-send
    // path to decide whether Initial+Handshake coalescing (RFC 9000
    // §12.2) is applicable in the current round.
    bool HasPendingDataAt(uint8_t level) const {
        if (level >= kNumEncryptionLevels) {
            return false;
        }
        const auto& buf = send_buffers_[level];
        return buf && buf->GetDataLength() > 0;
    }

    using crypto_stream_read_callback =
        std::function<void(std::shared_ptr<IBufferRead> buffer, int32_t err, uint16_t encryption_level)>;
    virtual void SetCryptoStreamReadCallBack(crypto_stream_read_callback cb) { recv_cb_ = cb; }

    // RFC 9000 §19.6: a CRYPTO frame's offset is a varint and the sum
    // offset + length must not exceed 2^62-1.
    static constexpr uint64_t kMaxCryptoOffset = (1ULL << 62) - 1;

protected:
    void OnCryptoFrame(std::shared_ptr<IFrame> frame);

private:
    // Consumes any buffered frames that are now contiguous with
    // next_read_offset_[level], trimming the already-delivered prefix of frames that
    // partially overlap. Returns the number of bytes appended to read_buffers_.
    uint64_t DrainOutOrderFrames(uint8_t level);

    // read buffers for each encryption level
    std::shared_ptr<common::MultiBlockBuffer> read_buffers_[kNumEncryptionLevels];

    // in order next data offset for each encryption level
    uint64_t next_read_offset_[kNumEncryptionLevels];
    // Ordered by offset so partially overlapping ranges can be located; an
    // unordered_map only supports exact-offset lookup, which stalls the handshake
    // forever when the peer sends overlapping (rather than exactly adjacent) frames.
    std::map<uint64_t, std::shared_ptr<IFrame>> out_order_frame_[kNumEncryptionLevels];
    // Bytes currently held in out_order_frame_[level], for the §7.5 cap.
    uint64_t out_order_bytes_[kNumEncryptionLevels];

    // local data send offset for each encryption level
    uint64_t send_offset_[kNumEncryptionLevels];
    std::shared_ptr<common::MultiBlockBuffer> send_buffers_[kNumEncryptionLevels];

    crypto_stream_read_callback recv_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_STREAM_CRYPTO_STREAM