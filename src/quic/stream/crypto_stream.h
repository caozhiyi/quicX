#ifndef QUIC_STREAM_CRYPTO_STREAM
#define QUIC_STREAM_CRYPTO_STREAM

#include <cstdint>
#include <deque>
#include <map>

#include "common/buffer/buffer_chunk.h"
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

    // Successfully processing a peer Handshake packet proves the peer derived
    // Handshake keys, which requires every byte of our Initial CRYPTO flight:
    // the peer has consumed it and dropped its Initial keys (RFC 9000 4.9.1),
    // so re-sending any of it can never be acknowledged. The connection layer
    // calls this from the Handshake dispatch point; it drops the Initial-level
    // send state so loss notifications on that level stop re-queueing.
    void MarkInitialConsumedByPeer();

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
        return !outgoing_[level].empty();
    }

    // ==== Test-only accessors (do not use in production code) ====
    // Segment counts for the retained_/outgoing_ deques; used to pin the
    // retained-prefix trimming (#2) and the difference re-queue (#4).
    size_t GetRetainedSegmentCountForTest(uint8_t level) const {
        return (level < kNumEncryptionLevels) ? retained_[level].size() : 0;
    }
    size_t GetOutgoingSegmentCountForTest(uint8_t level) const {
        return (level < kNumEncryptionLevels) ? outgoing_[level].size() : 0;
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

    // ---- Frame-level delivery: reliable CRYPTO via offset (RFC 9000 s7.5) ----
    //
    // SendControl fires kAcked/kLost on every tracked frame (IFrame::
    // SetDeliveryHandler / FireFrameDelivery). A lost frame's bytes are
    // re-queued as a fresh outgoing segment, so the normal send path sizes
    // them to whatever budget is left: the build-time 8.1 amp constraint in
    // ComputeSendPolicy then slices the flight into small packets, each
    // eliciting the peer's immediate handshake-space ACK. This replaces the
    // old packet-level re-encode path (which could only re-send the full
    // 1069 B datagram that never fit a ~500 B budget, deadlocking until the
    // peer's handshake timeout).

    // Bytes retained for retransmission. One chunk per Send() call slice;
    // chunks stay alive via shared_ptr so the pool never recycles bytes an
    // unacked (or re-queued) frame still points at — the old MoveReadPt
    // consumption model made re-reads impossible, which is why this exists.
    struct RetainedSegment {
        uint64_t offset;                              // CRYPTO offset of first byte
        uint32_t length;                              // bytes actually written into the chunk
        std::shared_ptr<common::BufferChunk> chunk;   // owns the bytes
        // NB: |length| is the Send()-time byte count, NOT chunk->GetLength():
        // the latter is the pool-block CAPACITY (typically 4 KiB). Using the
        // capacity as the extent inflated a segment's range far past its real
        // data, so a lost frame resolved against the WRONG chunk and its
        // re-emission read uninitialized pool memory — on the wire that was
        // CRYPTO frames with "6a6a0000..." heads, which rustls parses as
        // HelloRequest and kills the connection (quinn handshakecorruption,
        // 2026-09-19).
    };

    // A pending send: fresh data or a re-emitted lost range. Segments may be
    // partially drained; frames carry the segment's own offset, so re-queued
    // segments appearing out of order on the wire is legal (the peer
    // reassembles by offset).
    struct OutgoingSegment {
        // Both CRYPTO offsets are ABSOLUTE; pos is an offset into the chunk.
        // #3 (code review round 2): |length| used to be measured from the
        // CHUNK's start while |offset| was absolute, aligned only via the
        // implicit invariant offset - pos == segment start — the exact
        // misalignment that once underflowed seg_left and put uninitialized
        // pool memory on the wire (rustls parsed it as HelloRequest and
        // dropped the connection). end_offset removes that invariant: the
        // segment spans [end_offset - (end_offset - offset), end_offset) in
        // CRYPTO space and [pos, pos + (end_offset - offset)) in chunk space.
        uint64_t offset;                              // CRYPTO offset of NEXT byte to encode
        uint64_t end_offset;                          // CRYPTO offset one past the last byte
        uint32_t pos;                                 // consumed prefix within the chunk
        std::shared_ptr<common::BufferChunk> chunk;   // shares ownership with RetainedSegment
    };

    // Delivery notification handler body. kAcked advances the contiguous
    // acked prefix and trims both the re-queued segments (PruneAckedSegments)
    // and the retained prefix (#2: retained_ used to never shrink on
    // Handshake/1-RTT, leaking one pool block per Send() slice — every
    // NewSessionTicket on a long-lived connection). kLost re-queues only the
    // parts of the range NOT already queued (#4, difference instead of the
    // old full-containment check). Out-of-order safe: a kLost already covered
    // by the acked prefix was superseded by a late ACK and is ignored.
    void OnFrameDelivery(uint8_t level, uint64_t offset, uint32_t length, FrameDeliveryState state);
    // Drop re-queued segments fully covered by the acked prefix (late ACK
    // arriving after a kLost already re-queued the range).
    void PruneAckedSegments(uint8_t level);

    std::deque<RetainedSegment> retained_[kNumEncryptionLevels];
    std::deque<OutgoingSegment> outgoing_[kNumEncryptionLevels];
    // Contiguous acked prefix per level.
    uint64_t acked_offset_[kNumEncryptionLevels];
    // Set by MarkInitialConsumedByPeer(); Initial-level kLost is ignored after.
    bool initial_consumed_by_peer_{false};

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

    // local data send offset for each encryption level: the CRYPTO offset of
    // the next byte TLS will hand us (total bytes written per level).
    uint64_t send_offset_[kNumEncryptionLevels];

    crypto_stream_read_callback recv_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_STREAM_CRYPTO_STREAM