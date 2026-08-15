#include <algorithm>

#include "common/log/log.h"

#include "quic/connection/error.h"
#include "quic/frame/crypto_frame.h"
#include "quic/quicx/global_resource.h"
#include "quic/stream/crypto_stream.h"

namespace quicx {
namespace quic {

CryptoStream::CryptoStream(std::weak_ptr<common::IEventLoop> loop,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    IStream(loop, 0, active_send_cb, stream_close_cb, connection_close_cb) {
    for (int i = 0; i < kNumEncryptionLevels; i++) {
        next_read_offset_[i] = 0;
        out_order_bytes_[i] = 0;
        send_offset_[i] = 0;
        read_buffers_[i] =
            std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool());
        send_buffers_[i] = nullptr;
    }
}

CryptoStream::~CryptoStream() {}

IStream::TrySendResult CryptoStream::TrySendData(IFrameVisitor* visitor, EncryptionLevel level) {
    if (level >= kNumEncryptionLevels) {
        return IStream::TrySendResult::kFailed;
    }

    if (!send_buffers_[level] || send_buffers_[level]->GetDataLength() == 0) {
        // Bug fix (burst-mode handshake stall):
        // The current encryption level has no pending CRYPTO data, but data for
        // *other* levels (e.g. Handshake EE/Cert/CV/Fin while the caller is
        // still draining Initial) may still be waiting. StreamManager treats
        // kSuccess as "stream finished" and erases it from the active set, so
        // if we silently return here the CryptoStream is dropped even though
        // it still has work pending — the next level switch will never see it,
        // and the handshake stalls until the idle-timeout fires.
        //
        // In the legacy per-packet send path this was masked because each
        // TrySend() re-ran the encryption-level scheduler and the freshly
        // selected level usually matched a non-empty buffer. The burst-mode
        // send path reuses one selected level across multiple build attempts
        // within a single round, which is what exposed the latent bug.
        //
        // Fix: before signalling kSuccess, re-arm ToSend() if any other level
        // still has data, so StreamManager keeps the CryptoStream on the
        // active list for the next round.
        for (uint8_t i = 0; i < kNumEncryptionLevels; i++) {
            if (i == level) {
                continue;
            }
            if (send_buffers_[i] && send_buffers_[i]->GetDataLength() > 0) {
                ToSend();
                break;
            }
        }
        return IStream::TrySendResult::kSuccess;
    }

    // make crypto frame
    auto frame = std::make_shared<CryptoFrame>();
    frame->SetOffset(send_offset_[level]);
    frame->SetEncryptionLevel(level);

    // Per-datagram cap: cap CRYPTO frame payload by the current packet
    // buffer's real free space. CRYPTO frame header worst case:
    // type(1B) + offset(<=8B) + length(<=2B) = ~11B; reserve 20B for safety.
    // Replaces the historical hardcoded 1300 cap which left ~120B unused
    // on each packet (kMaxFramePayload is 1420).
    constexpr uint32_t kCryptoHeaderReserve = 20;
    uint32_t crypto_pkt_left = visitor->GetPacketLeftSize();
    uint32_t crypto_pkt_cap = crypto_pkt_left > kCryptoHeaderReserve ? crypto_pkt_left - kCryptoHeaderReserve : 0;

    uint32_t write_size = visitor->GetLeftStreamDataSize();
    if (write_size > crypto_pkt_cap) {
        write_size = crypto_pkt_cap;
    }
    if (write_size == 0) {
        // Visitor has not declared a stream-level cap yet (typical when
        // crypto handshake bytes flow before flow control is fully wired up).
        // Fall back to "as much as the buffer currently holds" so we still
        // make progress. Cap by the current packet's free space.
        write_size = std::min<uint32_t>(send_buffers_[level]->GetDataLength(), crypto_pkt_cap);
    }

    common::SharedBufferSpan data = send_buffers_[level]->GetFirstChunkReadable(write_size);
    if (!data.Valid()) {
        // No readable data despite GetDataLength() > 0 should not happen, but
        // guard defensively: simply report success and try again next round.
        LOG_DEBUG("CryptoStream::TrySendData: no readable data, level:%d", level);
        return IStream::TrySendResult::kSuccess;
    }
    frame->SetData(data);

    if (!visitor->HandleFrame(frame)) {
        LOG_WARN("CryptoStream::TrySendData: visitor handle frame failed. level:%d", level);
        return IStream::TrySendResult::kFailed;
    }

    LOG_DEBUG("CryptoStream::TrySendData: sent frame level:%d, offset:%llu, len:%d", level, send_offset_[level],
        data.GetLength());

    send_buffers_[level]->MoveReadPt(data.GetLength());
    send_offset_[level] += data.GetLength();

    // Check if we still have data for this level
    if (send_buffers_[level]->GetDataLength() > 0) {
        ToSend();
        return IStream::TrySendResult::kSuccess;
    }

    // Check if we have data for other levels
    for (uint8_t i = 0; i < kNumEncryptionLevels; i++) {
        if (i == level) {
            continue;
        }
        if (send_buffers_[i] && send_buffers_[i]->GetDataLength() > 0) {
            ToSend();
            break;
        }
    }

    return IStream::TrySendResult::kSuccess;
}

// reset the stream
void CryptoStream::Reset(uint32_t /*error*/) {
    // do nothing
}

void CryptoStream::ResetForRetry() {
    LOG_INFO("Resetting CryptoStream for Retry");

    // Clear Initial level state to restart handshake from offset 0
    uint8_t level = kInitial;

    // Reset read state
    read_buffers_[level] =
        std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool());
    next_read_offset_[level] = 0;
    out_order_frame_[level].clear();
    out_order_bytes_[level] = 0;

    // Reset send state
    send_buffers_[level] = nullptr;  // Next send will recreate it if needed
    send_offset_[level] = 0;
}

void CryptoStream::Close() {
    // do nothing
}

uint32_t CryptoStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    if (frame_type == FrameType::kCrypto) {
        OnCryptoFrame(frame);
        return 0;
    }
    // shouldn't be here
    LOG_ERROR("crypto stream recv error frame. type:%d", frame_type);
    return 0;
}

int32_t CryptoStream::Send(uint8_t* data, uint32_t len, uint8_t encryption_level) {
    auto loop = event_loop_.lock();
    if (!loop) return -1;
    if (!loop->IsInLoopThread()) {
        std::vector<uint8_t> vec(data, data + len);
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, vec = std::move(vec), encryption_level]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<CryptoStream>(self);
            if (stream) stream->Send(const_cast<uint8_t*>(vec.data()), vec.size(), encryption_level);
        });
        return len;
    }

    std::shared_ptr<common::MultiBlockBuffer> buffer = send_buffers_[encryption_level];
    if (!buffer) {
        buffer = std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool());
        send_buffers_[encryption_level] = buffer;
    }
    int32_t size = buffer->Write(data, len);

    ToSend();
    return size;
}

int32_t CryptoStream::Send(uint8_t* data, uint32_t len) {
    auto loop = event_loop_.lock();
    if (!loop) return -1;
    if (!loop->IsInLoopThread()) {
        std::vector<uint8_t> vec(data, data + len);
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, vec = std::move(vec)]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<CryptoStream>(self);
            if (stream) stream->Send(const_cast<uint8_t*>(vec.data()), vec.size());
        });
        return len;
    }

    return Send(data, len, GetWaitSendEncryptionLevel());
}

int32_t CryptoStream::Send(std::shared_ptr<IBufferRead> data) {
    auto loop = event_loop_.lock();
    if (!loop) return -1;
    if (!loop->IsInLoopThread()) {
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, data]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<CryptoStream>(self);
            if (stream) stream->Send(data);
        });
        return data->GetDataLength();
    }

    uint8_t level = GetWaitSendEncryptionLevel();
    std::shared_ptr<common::MultiBlockBuffer> buffer = send_buffers_[level];
    if (!buffer) {
        buffer = std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool());
        send_buffers_[level] = buffer;
    }
    int32_t size = buffer->Write(data);

    ToSend();
    return size;
}

uint8_t CryptoStream::GetWaitSendEncryptionLevel() {
    uint8_t level = kApplication;
    if (send_buffers_[kInitial] && send_buffers_[kInitial]->GetDataLength() > 0) {
        level = kInitial;
    } else if (send_buffers_[kHandshake] && send_buffers_[kHandshake]->GetDataLength() > 0) {
        level = kHandshake;
    }
    return level;
}

void CryptoStream::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    auto crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(frame);
    // CRITICAL: Use the level from the frame to select correct state
    // FrameProcessor/Connection layer MUST ensure this level is set
    uint8_t level = crypto_frame->GetEncryptionLevel();

    // Bounds check
    if (level >= kNumEncryptionLevels) {
        LOG_ERROR("CryptoStream received frame with invalid level %d", level);
        return;
    }

    LOG_INFO("CryptoStream::OnCryptoFrame: level=%d, offset=%llu, len=%u, expected=%llu", level,
        crypto_frame->GetOffset(), crypto_frame->GetLength(), next_read_offset_[level]);

    const uint64_t frame_offset = crypto_frame->GetOffset();
    const uint64_t frame_length = crypto_frame->GetLength();

    // RFC 9000 §19.6: "The largest offset delivered on a stream -- the sum of the
    // offset and data length -- cannot exceed 2^62-1 [...] receipt of a frame that
    // exceeds this limit MUST be treated as a connection error of type
    // FRAME_ENCODING_ERROR." This also covers the unsigned wraparound of
    // offset + length that would otherwise corrupt next_read_offset_.
    if (frame_offset > kMaxCryptoOffset || frame_length > kMaxCryptoOffset - frame_offset) {
        LOG_ERROR("crypto frame offset+length out of range. level:%d, offset:%llu, len:%llu", level, frame_offset,
            frame_length);
        if (connection_close_cb_) {
            connection_close_cb_(
                QuicErrorCode::kFrameEncodingError, frame->GetType(), "crypto frame offset+length out of range.");
        }
        return;
    }

    if (frame_offset <= next_read_offset_[level]) {
        // In-order, or overlapping data we have already delivered in part. Skip the
        // prefix we have seen and append only the new suffix; a pure duplicate
        // contributes nothing.
        const uint64_t already_have = next_read_offset_[level] - frame_offset;
        if (already_have < frame_length) {
            // IMPORTANT: Copy the bytes into read_buffers_ (do NOT push the shared
            // chunk). CRYPTO frames from a received packet share the packet's
            // decode buffer; that buffer is recycled once the packet is fully
            // processed, so holding a shared pointer is not enough to guarantee
            // the bytes remain stable. Copy into a fresh chunk owned by
            // read_buffers_[level] to avoid later corruption.
            auto data_span = crypto_frame->GetData();
            const uint32_t new_bytes = static_cast<uint32_t>(frame_length - already_have);
            read_buffers_[level]->Write(data_span.GetStart() + already_have, new_bytes);
            next_read_offset_[level] += new_bytes;
        }

        // Buffered frames may now be contiguous (possibly overlapping) with the
        // advanced offset.
        DrainOutOrderFrames(level);

        // Notify upper layer (TLS) with correct level
        if (recv_cb_) {
            recv_cb_(read_buffers_[level], 0, level);
        }
        return;
    }

    // Strictly ahead of the current offset: buffer it until the gap is filled.
    // Duplicates at the same offset are ignored so an existing (possibly longer)
    // buffered frame is not overwritten.
    auto& out_order = out_order_frame_[level];
    if (out_order.find(frame_offset) != out_order.end()) {
        return;
    }

    // RFC 9000 §7.5 cap. Enforced before allocating, so the attacker-controlled
    // frame cannot cause the allocation it is meant to prevent.
    if (out_order.size() >= kMaxOutOrderFrames || out_order_bytes_[level] + frame_length > kMaxOutOrderBytes) {
        LOG_ERROR("crypto out-of-order buffer exceeded. level:%d, frames:%zu, bytes:%llu, incoming:%llu", level,
            out_order.size(), out_order_bytes_[level], frame_length);
        if (connection_close_cb_) {
            connection_close_cb_(
                QuicErrorCode::kCryptoBufferExceeded, frame->GetType(), "crypto out-of-order buffer exceeded.");
        }
        return;
    }

    // Must also detach from packet buffer: copy into a standalone frame.
    auto data_span = crypto_frame->GetData();
    auto new_frame = std::make_shared<CryptoFrame>();
    new_frame->SetOffset(frame_offset);
    new_frame->SetEncryptionLevel(level);
    // Allocate a dedicated buffer and copy bytes so the span stays valid
    // after the source packet buffer is recycled.
    auto standalone = std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool());
    standalone->Write(data_span.GetStart(), static_cast<uint32_t>(frame_length));
    auto owned_span = standalone->GetSharedReadableSpan(static_cast<uint32_t>(frame_length));
    new_frame->SetData(owned_span);
    out_order[frame_offset] = new_frame;
    out_order_bytes_[level] += frame_length;
}

uint64_t CryptoStream::DrainOutOrderFrames(uint8_t level) {
    auto& out_order = out_order_frame_[level];
    uint64_t appended = 0;

    while (!out_order.empty()) {
        // Largest buffered offset that is not beyond the delivery point. Anything
        // above it leaves a genuine gap, so delivery stops there.
        auto upper = out_order.upper_bound(next_read_offset_[level]);
        if (upper == out_order.begin()) {
            break;
        }
        auto iter = std::prev(upper);

        auto buffered = std::dynamic_pointer_cast<CryptoFrame>(iter->second);
        if (!buffered) {
            out_order.erase(iter);
            continue;
        }

        const uint64_t buffered_offset = iter->first;
        const uint64_t buffered_length = buffered->GetLength();
        const uint64_t already_have = next_read_offset_[level] - buffered_offset;

        if (already_have < buffered_length) {
            auto span = buffered->GetData();
            const uint32_t new_bytes = static_cast<uint32_t>(buffered_length - already_have);
            read_buffers_[level]->Write(span.GetStart() + already_have, new_bytes);
            next_read_offset_[level] += new_bytes;
            appended += new_bytes;
        }
        // Fully superseded (or now consumed): release it either way.
        out_order_bytes_[level] -= std::min(out_order_bytes_[level], buffered_length);
        out_order.erase(iter);
    }

    return appended;
}

}  // namespace quic
}  // namespace quicx