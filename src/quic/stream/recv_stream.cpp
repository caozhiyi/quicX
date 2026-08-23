#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>

#include "common/log/log.h"

#include "quic/config.h"
#include "quic/connection/error.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/stream_data_blocked_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/quicx/global_resource.h"
#include "quic/stream/recv_stream.h"
#include "quic/stream/state_machine_recv.h"

namespace quicx {
namespace quic {

RecvStream::RecvStream(std::weak_ptr<common::IEventLoop> loop, uint64_t init_data_limit, uint64_t id,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    IStream(loop, id, active_send_cb, stream_close_cb, connection_close_cb),
    final_offset_(0),
    local_data_limit_(init_data_limit),
    except_offset_(0),
    out_order_bytes_(0),
    reset_error_(0) {
    buffer_ = std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool());
    recv_machine_ = std::make_shared<StreamStateMachineRecv>();
}

RecvStream::~RecvStream() {}

void RecvStream::Reset(uint32_t error) {
    auto loop = event_loop_.lock();
    if (!loop) return;
    if (!loop->IsInLoopThread()) {
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, error]() {
            auto self = weak_self.lock();
            if (!self) return;
            self->Reset(error);
        });
        return;
    }

    // RFC 9000: Only send STOP_SENDING when there's an actual error (error != 0)
    // When error == 0, it means normal completion (received FIN from peer)
    // In that case, do nothing - no need to send STOP_SENDING
    if (error != 0) {
        // Check if we can send STOP_SENDING frame in current state
        if (!recv_machine_->CheckCanSendFrame(FrameType::kStopSending)) {
            LOG_WARN("stream recv cannot send STOP_SENDING in current state. stream id:%d", stream_id_);
            return;
        }

        auto stop_frame = std::make_shared<StopSendingFrame>();
        stop_frame->SetStreamID(stream_id_);
        stop_frame->SetAppErrorCode(error);

        frames_list_.emplace_back(stop_frame);

        // Transition state machine to kResetRecvd when locally initiated reset
        recv_machine_->OnFrame(FrameType::kResetStream);

        ToSend();
        LOG_DEBUG("stream recv reset due to error. stream id:%d, error:%d", stream_id_, error);
    } else {
        LOG_DEBUG("stream recv complete normally (received FIN). stream id:%d", stream_id_);
    }
}

void RecvStream::SetStreamReadCallBack(stream_read_callback cb) {
    recv_cb_ = cb;
    if (!recv_cb_ || flush_pending_data_posted_) {
        return;
    }

    // Data may already have arrived before anybody was listening: the application
    // layer (HTTP/3) is only wired up once the handshake completes, whereas the
    // peer's control / QPACK unidirectional streams typically arrive in the very
    // same flight. Without this catch-up delivery those bytes stay in buffer_
    // forever and the stream silently stalls.
    if (!buffer_ || buffer_->GetDataLength() == 0) {
        return;
    }

    // Deliver asynchronously. SetStreamReadCallBack() is normally called from the
    // constructor of the owning application object, which the caller only
    // registers *after* the constructor returns. Invoking the callback inline
    // would re-enter that half-initialised object and let the caller overwrite
    // the object it just spawned, leaving a dangling `this` captured in the read
    // callback (use-after-free on the next STREAM frame). Posting to the loop
    // preserves the "callback fires after the owner is fully registered"
    // invariant.
    auto loop = event_loop_.lock();
    if (!loop) {
        return;
    }

    // PostTask, not RunInLoop: RunInLoop executes inline when already on the loop
    // thread, which is exactly the re-entrancy we must avoid here.
    flush_pending_data_posted_ = true;
    auto weak_self = weak_from_this();
    loop->PostTask([weak_self]() {
        auto self = std::dynamic_pointer_cast<RecvStream>(weak_self.lock());
        if (!self) {
            return;
        }
        self->FlushBufferedData();
    });
}

void RecvStream::FlushBufferedData() {
    flush_pending_data_posted_ = false;
    if (!recv_cb_ || !buffer_ || buffer_->GetDataLength() == 0) {
        // A regular STREAM frame may have overtaken us and already drained the
        // buffer through the normal path; nothing left to hand over.
        return;
    }

    bool is_last = (has_final_offset_ && final_offset_ == except_offset_ && out_order_frame_.empty());
    LOG_DEBUG("RecvStream::FlushBufferedData delivering buffered data. stream id:%llu, buffer_len:%u, is_last:%d",
        stream_id_, buffer_->GetDataLength(), is_last);

    recv_cb_(buffer_, is_last, reset_error_);

    if (recv_machine_->CanAppReadAllData()) {
        recv_machine_->AppReadAllData();
    }
}

uint32_t RecvStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    switch (frame_type) {
        case FrameType::kStreamDataBlocked:
            OnStreamDataBlockFrame(frame);
            break;
        case FrameType::kResetStream:
            OnResetStreamFrame(frame);
            break;
        default:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                return OnStreamFrame(frame);
            } else {
                LOG_ERROR("unexpected frame on recv stream. frame type:%d", frame_type);
            }
    }
    return 0;
}

IStream::TrySendResult RecvStream::TrySendData(IFrameVisitor* visitor) {
    IStream::TrySendData(nullptr);

    for (auto iter = frames_list_.begin(); iter != frames_list_.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = frames_list_.erase(iter);

        } else {
            // Check if the failure was due to insufficient packet space
            if (visitor->GetLastError() == FrameEncodeError::kInsufficientSpace) {
                LOG_INFO("recv stream: packet full, need retry. stream id:%d", stream_id_);
                return TrySendResult::kBreak;  // Packet is full, stream needs to retry in next packet
            }
            return TrySendResult::kFailed;
        }
    }
    LOG_DEBUG("stream recv try send data. stream id:%d", stream_id_);
    return TrySendResult::kSuccess;
}

uint32_t RecvStream::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    if (!recv_machine_->OnFrame(frame->GetType())) {
        LOG_WARN("stream recv can't process stream frame. stream id:%d, frame type:%d", stream_id_, frame->GetType());
        return 0;
    }

    // check flow control
    auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
    // Guard against integer overflow: offset + length could wrap around uint64_t
    uint64_t frame_end = stream_frame->GetOffset() + stream_frame->GetLength();
    if (frame_end < stream_frame->GetOffset()) {
        // Integer overflow detected
        if (connection_close_cb_) {
            connection_close_cb_(
                QuicErrorCode::kFlowControlError, frame->GetType(), "stream frame offset+length integer overflow.");
        }
        LOG_ERROR("stream frame offset+length overflow. stream id:%d, offset:%llu, length:%u", stream_id_,
            stream_frame->GetOffset(), stream_frame->GetLength());
        return 0;
    }
    if (frame_end > local_data_limit_) {
        if (connection_close_cb_) {
            connection_close_cb_(
                QuicErrorCode::kFlowControlError, frame->GetType(), "stream recv data exceeding flow control limits.");
        }
        LOG_WARN("stream recv data exceeding flow control limits. stream id:%llu, offset:%llu, length:%u, limit:%llu",
            stream_id_, stream_frame->GetOffset(), stream_frame->GetLength(), local_data_limit_);
        return 0;
    }

    // check stream frame whit fin
    if (stream_frame->IsFin()) {
        uint64_t fin_offset = stream_frame->GetOffset() + stream_frame->GetLength();
        if (has_final_offset_ && fin_offset != final_offset_) {
            LOG_ERROR("invalid final size. size:%d", fin_offset);
            if (connection_close_cb_) {
                connection_close_cb_(QuicErrorCode::kFinalSizeError, frame->GetType(), "final size change.");
            }
            LOG_DEBUG("stream recv invalid final size. stream id:%d, fin offset:%d, final offset:%d", stream_id_,
                fin_offset, final_offset_);
            return 0;
        }
        final_offset_ = fin_offset;
        has_final_offset_ = true;
    }

    LOG_DEBUG("stream recv stream frame. stream id:%llu, offset:%llu, length:%u, final offset:%llu", stream_id_,
        stream_frame->GetOffset(), stream_frame->GetLength(), final_offset_);

    if (stream_frame->GetOffset() <= except_offset_) {
        // RFC 9000 §2.2: STREAM frames carrying overlapping ranges of stream
        // data are legal. A loss-recovering sender frequently re-segments data:
        // the retransmission may start before except_offset_ (already received
        // prefix) yet carry brand-new bytes past it. Dropping such a frame
        // whole permanently loses the new tail: the sender has packet-level
        // ACKs for it and will never resend those bytes, so except_offset_
        // stalls forever and the stream never completes (observed as tail-stall
        // deadlock under transferloss/transfercorruption).
        if (stream_frame->GetOffset() + stream_frame->GetLength() <= except_offset_) {
            // Senders may resend the FIN as a zero-length STREAM frame after the
            // data itself was delivered by other (re-segmented) frames — quic-go
            // does this under loss. Swallowing it as a duplicate would leave the
            // stream forever incomplete. Normalize to an empty frame at
            // except_offset_ and fall through so the FIN can complete delivery.
            if (!stream_frame->IsFin()) {
                LOG_DEBUG("stream recv fully duplicate frame. stream id:%llu, offset:%llu, except offset:%llu", stream_id_,
                    stream_frame->GetOffset(), except_offset_);
                return 0;
            }
            common::SharedBufferSpan empty_span;
            stream_frame->SetData(empty_span);
            stream_frame->SetOffset(except_offset_);
        }
        uint64_t overlap = except_offset_ - stream_frame->GetOffset();
        auto raw = stream_frame->GetData();
        common::SharedBufferSpan trimmed(raw.GetChunk(), raw.GetStart() + overlap, raw.GetEnd());
        stream_frame->SetData(trimmed);
        stream_frame->SetOffset(except_offset_);

        // CRITICAL: Use deep copy (Write(uint8_t*, len)) instead of shallow copy (Write(SharedBufferSpan, len)).
        // SharedBufferSpan points into the packet buffer chunk, which may be shared by multiple STREAM frames
        // from the same packet (belonging to different streams). If we shallow-copy (push the chunk directly
        // into our buffer's chunk list), subsequent Write operations may use the "free" space after our data
        // in that chunk — but that space may contain another stream's data. This causes cross-stream
        // data contamination. Deep copy avoids this by copying data into our own exclusive chunks.
        // Zero-length FIN-only frames carry no bytes to copy.
        if (stream_frame->GetLength() > 0) {
            buffer_->Write(stream_frame->GetData().GetStart(), stream_frame->GetLength());
        }
        except_offset_ += stream_frame->GetLength();

        // Absorb the out-of-order queue. Iterate from the LOWEST buffered
        // offset (map is sorted) instead of exact-key find(except_offset_):
        // buffered frames may also partially overlap except_offset_ and must
        // be prefix-trimmed rather than skipped, otherwise they stay stuck in
        // the queue forever.
        while (!out_order_frame_.empty()) {
            auto iter = out_order_frame_.begin();
            auto queued = std::dynamic_pointer_cast<StreamFrame>(iter->second);
            uint64_t qs = queued->GetOffset();
            uint64_t qe = qs + queued->GetLength();
            if (qe <= except_offset_) {
                // Entirely below the consumed point: pure duplicate.
                out_order_bytes_ -= queued->GetLength();
                out_order_frame_.erase(iter);
                continue;
            }
            if (qs < except_offset_) {
                // Partial overlap: drop the received prefix, re-key the frame
                // at except_offset_ and re-examine (it may now be contiguous).
                uint64_t qov = except_offset_ - qs;
                auto qraw = queued->GetData();
                common::SharedBufferSpan qtrimmed(qraw.GetChunk(), qraw.GetStart() + qov, qraw.GetEnd());
                queued->SetData(qtrimmed);
                queued->SetOffset(except_offset_);
                out_order_bytes_ -= qov;
                out_order_frame_.erase(iter);
                out_order_frame_[except_offset_] = queued;
                continue;
            }
            if (qs > except_offset_) {
                break;  // real gap: nothing more can be reassembled yet
            }
            // Contiguous continuation.
            out_order_bytes_ -= queued->GetLength();
            buffer_->Write(queued->GetData().GetStart(), queued->GetLength());
            except_offset_ += queued->GetLength();
            out_order_frame_.erase(iter);
        }

        bool is_last = false;
        if (has_final_offset_ && final_offset_ == except_offset_ && out_order_frame_.empty()) {
            is_last = true;
            recv_machine_->RecvAllData();
        }

        LOG_DEBUG(
            "RecvStream::OnStreamFrame triggering recv_cb_. stream id:%d, has_cb:%d, is_last:%d, "
            "buffer_len:%d, final_offset:%d, except_offset:%d, out_order_size:%d",
            stream_id_, (recv_cb_ ? 1 : 0), is_last, buffer_->GetDataLength(), final_offset_, except_offset_,
            (int)out_order_frame_.size());

        if (recv_cb_) {
            recv_cb_(buffer_, is_last, reset_error_);
        }

        if (recv_machine_->CanAppReadAllData()) {
            recv_machine_->AppReadAllData();
        }

    } else {
        // RFC 9000 Section 4.6: If a RESET_STREAM or STREAM frame
        // is received indicating a change in the final size for the stream, an endpoint MUST respond with
        // an error of type FINAL_SIZE_ERROR.
        if (has_final_offset_ && stream_frame->GetOffset() > final_offset_) {
            LOG_ERROR("stream recv data out of final size. stream id:%d, offset:%d, final offset:%d", stream_id_,
                stream_frame->GetOffset(), final_offset_);
            if (connection_close_cb_) {
                connection_close_cb_(QuicErrorCode::kFinalSizeError, frame->GetType(), "data out of final size.");
            }
            return 0;
        }

        // RFC 9000 Section 2.2: The data at a given offset MUST NOT change if it is sent multiple times.
        if (out_order_frame_.find(stream_frame->GetOffset()) != out_order_frame_.end() ||
            stream_frame->GetOffset() < except_offset_) {
            LOG_DEBUG("stream recv repeat packet. stream id:%d, offset:%d", stream_id_, stream_frame->GetOffset());
            return 0;
        }

        // Memory-bounded out-of-order buffering.
        // QUIC streams tolerate out-of-order delivery (RFC 9000 §2.2), so a large
        // number of buffered frames is NOT a protocol violation and we must NOT
        // CONNECTION_CLOSE here. The previous code closed the connection once
        // >= kMaxOutOfOrderFrames (1024) frames piled up behind a single lost gap,
        // which killed legitimate large transfers (P0: quicx self-loop
        // transfer/chacha20/rebind-port/rebind-addr/connectionmigration). We now
        // bound memory by *bytes* and, when over budget, evict the oldest
        // (lowest-offset) buffered frame. Loss recovery retransmits the evicted
        // data, so the connection survives and the stream still completes. 32MB
        // is far above any single-stream transfer, so normal traffic never
        // triggers eviction.
        uint32_t frame_len = stream_frame->GetLength();
        // Bound the out-of-order buffer by *bytes* (memory), not by frame count.
        // QUIC streams tolerate out-of-order delivery (RFC 9000 §2.2), so a large
        // number of buffered frames is NOT a protocol violation and we must NOT
        // CONNECTION_CLOSE here (the old kMaxOutOfOrderFrames=1024 frame-count
        // limit killed legitimate large transfers — P0: quicx self-loop
        // transfer/chacha20/rebind-port/rebind-addr/connectionmigration).
        //
        // We must NOT evict older buffered frames: out-of-order frames are
        // already acknowledged at the packet level, so dropping one loses that
        // data permanently (the sender will not retransmit ACKed data) and the
        // stream stalls. The only memory bound we can enforce is to close the
        // connection once the byte budget is exceeded. A 32MB budget is far above
        // any legitimate single-stream transfer (the 1MB interop files stay well
        // under it), so legitimate traffic never triggers this; reaching it means
        // a peer is sending an unbounded amount of unreassemblable out-of-order
        // data (DoS / broken peer), which is a real flow-control violation worth
        // closing on. This replaces the old kMaxOutOfOrderFrames=1024 frame-count
        // limit, whose threshold was too tight and killed legitimate large
        // transfers (P0: quicx self-loop transfer/chacha20/rebind-*/connectionmigration).
        if (out_order_bytes_ + frame_len > kMaxOutOfOrderBytes) {
            LOG_ERROR("out-of-order buffer exceeded byte limit. stream id:%d, buffered bytes:%llu, "
                      "frame len:%u, limit:%llu",
                stream_id_, out_order_bytes_, frame_len, (uint64_t)kMaxOutOfOrderBytes);
            if (connection_close_cb_) {
                connection_close_cb_(
                    QuicErrorCode::kFlowControlError, frame->GetType(), "out-of-order buffer exceeded byte limit");
            }
            return 0;
        }
        out_order_frame_[stream_frame->GetOffset()] = stream_frame;
        out_order_bytes_ += frame_len;
    }

    // Proactive flow control window update strategy:
    // - Send MAX_STREAM_DATA early (when 25% consumed) to prevent sender stalling
    // - Use large increments to reduce frequency of updates
    // - This allows sender to continue at full speed without waiting for window updates
    const uint64_t kWindowThreshold = local_data_limit_ / 4;   // Trigger at 25% remaining (proactive)
    const uint64_t kWindowIncrement = kStreamWindowIncrement;  // Use configured increment for high throughput

    uint64_t remaining_window = local_data_limit_ - except_offset_;
    if (remaining_window < kWindowThreshold) {
        if (recv_machine_->CheckCanSendFrame(FrameType::kMaxStreamData)) {
            // Calculate increment to restore window to a healthy size
            // Aim for at least 2MB available window after update
            uint64_t target_window = kStreamWindowIncrement;
            uint64_t needed =
                (target_window > remaining_window) ? (target_window - remaining_window) : kWindowIncrement;
            // Round up to nearest kWindowIncrement
            needed = ((needed + kWindowIncrement - 1) / kWindowIncrement) * kWindowIncrement;

            local_data_limit_ += needed;
            auto max_frame = std::make_shared<MaxStreamDataFrame>();
            max_frame->SetStreamID(stream_id_);
            max_frame->SetMaximumData(local_data_limit_);
            frames_list_.emplace_back(max_frame);

            ToSend();
            LOG_DEBUG("Proactive flow control update: stream_id=%llu, new_limit=%llu, consumed=%llu, added=%llu",
                stream_id_, local_data_limit_, except_offset_, needed);
        }
    }

    // Metrics: Stream data received
    common::Metrics::CounterInc(common::MetricsStd::QuicStreamsBytesRx, stream_frame->GetLength());

    return stream_frame->GetLength();
}

void RecvStream::OnStreamDataBlockFrame(std::shared_ptr<IFrame> frame) {
    if (!recv_machine_->OnFrame(frame->GetType())) {
        LOG_WARN("stream recv can't process stream data blocked frame. stream id:%d, frame type:%d", stream_id_,
            frame->GetType());
        return;
    }

    auto block_frame = std::dynamic_pointer_cast<StreamDataBlockedFrame>(frame);

    // When peer is blocked, increase window significantly to allow high throughput
    // Use configured increments (see quic/config.h)

    if (local_data_limit_ >= kMaxStreamWindowSize) {
        LOG_WARN("stream recv window already at max. stream id:%d, limit:%llu", stream_id_, local_data_limit_);
        return;
    }
    local_data_limit_ = std::min(local_data_limit_ + kBlockedWindowIncrement, kMaxStreamWindowSize);

    auto max_frame = std::make_shared<MaxStreamDataFrame>();
    max_frame->SetStreamID(stream_id_);
    max_frame->SetMaximumData(local_data_limit_);
    frames_list_.emplace_back(max_frame);

    LOG_DEBUG("stream recv data blocked, increased window. stream id:%d, blocked_at:%llu, new_limit:%llu", stream_id_,
        block_frame->GetMaximumData(), local_data_limit_);

    ToSend();
}

void RecvStream::OnResetStreamFrame(std::shared_ptr<IFrame> frame) {
    if (!recv_machine_->OnFrame(frame->GetType())) {
        LOG_WARN(
            "stream recv can't process reset stream frame. stream id:%d, frame type:%d", stream_id_, frame->GetType());
        return;
    }

    auto reset_frame = std::dynamic_pointer_cast<ResetStreamFrame>(frame);
    uint64_t fin_offset = reset_frame->GetFinalSize();
    LOG_DEBUG("stream recv reset stream. stream id:%llu, fin offset:%llu, final offset:%llu", stream_id_, fin_offset,
        final_offset_);

    if (has_final_offset_ && fin_offset != final_offset_) {
        LOG_ERROR("stream recv invalid final size. stream id:%d, fin offset:%d, final offset:%d", stream_id_,
            fin_offset, final_offset_);
        if (connection_close_cb_) {
            connection_close_cb_(QuicErrorCode::kFinalSizeError, frame->GetType(), "final size change.");
        }
        return;
    }

    final_offset_ = fin_offset;
    has_final_offset_ = true;

    if (recv_machine_->GetStatus() == StreamState::kResetRecvd) {
        if (recv_cb_) {
            recv_cb_(buffer_, false, reset_frame->GetAppErrorCode());
        }

    } else {
        reset_error_ = reset_frame->GetAppErrorCode();
    }

    // Metrics: RESET_STREAM received
    common::Metrics::CounterInc(common::MetricsStd::QuicStreamsResetRx);
}

}  // namespace quic
}  // namespace quicx
