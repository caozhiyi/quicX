#include <algorithm>
#include <cstdint>

#include "common/log/log.h"

#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>

#include "quic/config.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/stream_data_blocked_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/quicx/global_resource.h"
#include "quic/stream/send_stream.h"
#include "quic/stream/state_machine_send.h"

namespace quicx {
namespace quic {

SendStream::SendStream(std::weak_ptr<common::IEventLoop> loop, uint64_t init_data_limit, uint64_t id,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    IStream(loop, id, active_send_cb, stream_close_cb, connection_close_cb),
    to_fin_(false),
    send_data_offset_(0),
    acked_offset_(0),
    fin_sent_(false),
    peer_data_limit_(init_data_limit),
    blocked_at_limit_(0),  // Initialize to 0 - means STREAM_DATA_BLOCKED not sent yet
    send_buffer_(std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool())) {
    send_machine_ = std::make_shared<StreamStateMachineSend>();
}

SendStream::~SendStream() {}

void SendStream::Close() {
    auto loop = event_loop_.lock();
    if (!loop) return;
    if (!loop->IsInLoopThread()) {
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<SendStream>(self);
            if (stream) {
                stream->Close();
            }
        });
        return;
    }

    common::LOG_DEBUG("SendStream::Close: stream id:%llu", stream_id_);
    to_fin_ = true;
    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
}

void SendStream::Reset(uint32_t error) {
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

    // check status
    if (!send_machine_->OnFrame(FrameType::kResetStream)) {
        return;
    }

    auto frame = std::make_shared<ResetStreamFrame>();
    frame->SetStreamID(stream_id_);
    frame->SetFinalSize(send_data_offset_);
    frame->SetAppErrorCode(error);
    common::LOG_DEBUG("stream send reset stream. stream id:%llu, error:%u", stream_id_, error);
    frames_list_.emplace_back(frame);
    ToSend();

    // Metrics: RESET_STREAM sent
    common::Metrics::CounterInc(common::MetricsStd::QuicStreamsResetTx);
}

int32_t SendStream::Send(uint8_t* data, uint32_t len) {
    auto loop = event_loop_.lock();
    if (!loop) return -1;
    common::LOG_DEBUG("SendStream::Send called: stream_id=%llu, len=%u, IsInLoopThread=%d", GetStreamID(), len,
        loop->IsInLoopThread());

    if (!loop->IsInLoopThread()) {
        common::LOG_WARN("SendStream::Send called from wrong thread, posting to EventLoop: stream_id=%llu, len=%u",
            GetStreamID(), len);
        std::vector<uint8_t> vec(data, data + len);
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, vec = std::move(vec)]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<SendStream>(self);
            if (stream) {
                common::LOG_DEBUG("SendStream::Send async execution in EventLoop: stream_id=%llu, len=%zu",
                    stream->GetStreamID(), vec.size());
                stream->Send(const_cast<uint8_t*>(vec.data()), vec.size());
            }
        });
        return len;
    }

    if (!send_machine_->CheckCanSendFrame(FrameType::kStream)) {
        common::LOG_DEBUG("stream send flush failed, the state can't send stream frame. stream id:%llu,", stream_id_);
        return -1;
    }

    int32_t ret = send_buffer_->Write(data, len);
    common::LOG_DEBUG("SendStream::Send: wrote to buffer, stream_id=%llu, requested=%u, written=%d, buffer_size=%u",
        GetStreamID(), len, ret, send_buffer_->GetDataLength());

    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }

    return ret;
}

int32_t SendStream::Send(std::shared_ptr<IBufferRead> buffer) {
    auto loop = event_loop_.lock();
    if (!loop) return -1;
    if (!loop->IsInLoopThread()) {
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, buffer]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<SendStream>(self);
            if (stream) {
                stream->Send(buffer);
            }
        });
        return buffer->GetDataLength();
    }

    if (!send_machine_->CheckCanSendFrame(FrameType::kStream)) {
        return -1;
    }

    int32_t ret = send_buffer_->Write(buffer);
    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
    return ret;
}

std::shared_ptr<IBufferWrite> SendStream::GetSendBuffer() {
    return std::dynamic_pointer_cast<IBufferWrite>(send_buffer_);
}

bool SendStream::Flush() {
    auto loop = event_loop_.lock();
    if (!loop) return false;
    if (!loop->IsInLoopThread()) {
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<SendStream>(self);
            if (stream) {
                stream->Flush();
            }
        });
        return true;
    }

    if (!send_machine_->CheckCanSendFrame(FrameType::kStream)) {
        common::LOG_DEBUG("stream send flush failed, the state can't send stream frame. stream id:%llu,", stream_id_);
        return false;
    }

    if (send_buffer_->GetDataLength() == 0) {
        common::LOG_DEBUG(
            "stream send flush failed. stream id:%llu, buffer length:%u", stream_id_, send_buffer_->GetDataLength());
        return false;
    }

    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
    return true;
}

uint32_t SendStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    switch (frame_type) {
        case FrameType::kMaxStreamData:
            OnMaxStreamDataFrame(frame);
            break;
        case FrameType::kStopSending:
            OnStopSendingFrame(frame);
            break;
        default:
            common::LOG_ERROR("unexpected frame on send stream. frame type:%d", frame_type);
    }
    return 0;
}

IStream::TrySendResult SendStream::TrySendData(IFrameVisitor* visitor, EncryptionLevel level) {
    IStream::TrySendData(nullptr, level);

    // check peer limit (guard against unsigned underflow when send_data_offset_ > peer_data_limit_)
    if (peer_data_limit_ > send_data_offset_ && peer_data_limit_ - send_data_offset_ < kStreamDataBlockedThreshold) {
        // Only send STREAM_DATA_BLOCKED once per limit value
        // When peer sends MAX_STREAM_DATA with new limit, blocked_at_limit_ will be less than new peer_data_limit_
        if (blocked_at_limit_ != peer_data_limit_ && send_machine_->CheckCanSendFrame(FrameType::kStreamDataBlocked)) {
            // make stream block frame
            std::shared_ptr<StreamDataBlockedFrame> frame = std::make_shared<StreamDataBlockedFrame>();
            frame->SetStreamID(stream_id_);
            frame->SetMaximumData(peer_data_limit_);
            common::LOG_DEBUG(
                "stream send data blocked. stream id:%llu, peer data limit:%llu", stream_id_, peer_data_limit_);

            // Metrics: Stream blocked by flow control
            common::Metrics::CounterInc(common::MetricsStd::QuicStreamDataBlocked);

            if (!visitor->HandleFrame(frame)) {
                common::LOG_DEBUG(
                    "stream send data blocked failed. stream id:%d, frame type:%d", stream_id_, frame->GetType());
                return TrySendResult::kFailed;
            }
            // Mark that we've sent STREAM_DATA_BLOCKED for this limit
            blocked_at_limit_ = peer_data_limit_;
        }
    }

    if (peer_data_limit_ <= send_data_offset_) {
        common::LOG_DEBUG("stream send data flow control blocked. stream id:%llu, peer data limit:%llu, send data offset:%llu", stream_id_,
            peer_data_limit_, send_data_offset_);
        return TrySendResult::kFlowControlBlocked;  // Keep in active list, waiting for MAX_STREAM_DATA
    }

    for (auto iter = frames_list_.begin(); iter != frames_list_.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = frames_list_.erase(iter);

        } else {
            common::LOG_DEBUG("stream send data failed. stream id:%d, frame type:%d", stream_id_, (*iter)->GetType());
            return TrySendResult::kFailed;
        }
    }

    if (!send_machine_->CheckCanSendFrame(FrameType::kStream)) {
        common::LOG_DEBUG("stream send data success. stream id:%d, can send stream frame:%d", stream_id_,
            send_machine_->CheckCanSendFrame(FrameType::kStream));
        return TrySendResult::kSuccess;
    }

    // make stream frame
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(stream_id_);
    frame->SetOffset(send_data_offset_);
    uint32_t send_size = 0;
    if (send_buffer_->GetDataLength() > 0) {
        // Guard against unsigned underflow: peer_data_limit_ >= send_data_offset_ is guaranteed
        // by the check above (line 209), but use safe subtraction with uint64_t to avoid truncation
        uint64_t stream_send_size_64 = (peer_data_limit_ > send_data_offset_) ? (peer_data_limit_ - send_data_offset_) : 0;
        uint32_t stream_send_size = static_cast<uint32_t>(std::min(stream_send_size_64, static_cast<uint64_t>(UINT32_MAX)));
        uint32_t conn_send_size = visitor->GetLeftStreamDataSize();
        send_size = stream_send_size > conn_send_size ? conn_send_size : stream_send_size;
        send_size = send_size > 1300 ? 1300 : send_size;  // TODO: 1300 is the max length of a stream frame

        common::LOG_DEBUG("stream send calc: stream_id:%d, peer_limit:%llu, send_offset:%llu, stream_send_size:%u, conn_send_size:%u, final_send_size:%u",
            stream_id_, peer_data_limit_, send_data_offset_, stream_send_size, conn_send_size, send_size);

        // Only try to get data if we have space to send
        if (send_size > 0) {
            common::SharedBufferSpan data = send_buffer_->GetSharedReadableSpan(send_size);
            if (data.Valid()) {
                frame->SetData(data);
            }
        }
    }

    bool has_fin = false;
    if (to_fin_) {
        if (send_buffer_->GetDataLength() - frame->GetData().GetLength() == 0) {
            frame->SetFin();
            has_fin = true;
        }
    }

    // If no data and no FIN to send, check why
    if (frame->GetData().GetLength() == 0 && !has_fin) {
        // Check if we have data in buffer but couldn't send due to flow control
        if (send_buffer_->GetDataLength() > 0) {
            // We hit one of three causes:
            //   (a) STREAM-LEVEL flow control: peer_data_limit_ - send_data_offset_
            //       is small (or zero) -> stream_send_size dominates the min().
            //   (b) CONNECTION-LEVEL flow control: visitor->GetLeftStreamDataSize()
            //       is 0 because BaseConnection::TrySend computed
            //       max_stream_data_size = min(max_bytes /*cwnd*/, conn_flow_limit) = 0.
            //   (c) CWND exhaustion: same path as (b), max_bytes=0.
            //
            // The previous code unconditionally treated all three as (a)
            // and emitted a STREAM_DATA_BLOCKED at peer_data_limit_, which
            // (i) is incorrect — the peer's stream-level limit is usually
            //     far above the offset; quic-go logs the bogus blocked
            //     frame at limit=524288 while we're only at offset=388276.
            // (ii) returns kFlowControlBlocked, which makes
            //     StreamManager::BuildStreamFrames flag every active stream
            //     as flow-control-blocked, masking the real cause from the
            //     connection-level recovery path (cwnd-limited / conn-FC
            //     blocked).
            //
            // Correct attribution:
            //   - Only emit STREAM_DATA_BLOCKED when the *stream-level*
            //     slack genuinely is below kStreamDataBlockedThreshold.
            //   - Otherwise the cause is connection-level (cwnd or conn-FC);
            //     return kBreak so the stream stays in the active set, and
            //     let BaseConnection::TrySend's existing cwnd-limited /
            //     conn-FC-blocked recovery (DATA_BLOCKED + recheck timer +
            //     ACK-driven send_retry_cb_) bring us back.
            uint64_t stream_slack =
                (peer_data_limit_ > send_data_offset_) ? (peer_data_limit_ - send_data_offset_) : 0;
            bool stream_level_blocked = stream_slack < kStreamDataBlockedThreshold;

            common::LOG_DEBUG("stream send data: zero-data frame. stream id:%d, buffer_len:%d, "
                              "peer_limit:%llu, send_offset:%llu, stream_slack:%llu, stream_level_blocked:%d",
                stream_id_, send_buffer_->GetDataLength(), peer_data_limit_, send_data_offset_,
                stream_slack, stream_level_blocked ? 1 : 0);

            if (stream_level_blocked) {
                // Genuine stream-level FC. RFC 9000 §19.13: STREAM_DATA_BLOCKED
                // is informational; de-dup with blocked_at_limit_ so we never
                // emit twice at the same limit.
                if (blocked_at_limit_ != peer_data_limit_ &&
                    send_machine_->CheckCanSendFrame(FrameType::kStreamDataBlocked)) {
                    auto blocked_frame = std::make_shared<StreamDataBlockedFrame>();
                    blocked_frame->SetStreamID(stream_id_);
                    blocked_frame->SetMaximumData(peer_data_limit_);
                    visitor->HandleFrame(blocked_frame);
                    blocked_at_limit_ = peer_data_limit_;
                    common::LOG_DEBUG("stream send: sent STREAM_DATA_BLOCKED frame. stream id:%d, limit:%llu",
                        stream_id_, peer_data_limit_);
                }
                return TrySendResult::kFlowControlBlocked;
            }

            // Connection-level back-pressure (cwnd or conn-FC). Stay in the
            // active set; do NOT emit STREAM_DATA_BLOCKED. The connection
            // layer is responsible for emitting DATA_BLOCKED and arming
            // its recheck timer (see connection_base.cpp Bug #17 path).
            return TrySendResult::kBreak;
        }
        // No data in buffer, truly nothing to send
        common::LOG_DEBUG("stream send data: no data to send. stream id:%d", stream_id_);
        return TrySendResult::kSuccess;
    }

    // Try to send the frame first, before updating state machine
    if (!visitor->HandleFrame(frame)) {
        // Check if the failure was due to insufficient packet space
        if (visitor->GetLastError() == FrameEncodeError::kInsufficientSpace) {
            common::LOG_INFO(
                "stream send data: packet full, need retry. stream id:%d, frame type:%d", stream_id_, frame->GetType());
            return TrySendResult::kBreak;  // Packet is full, stream needs to retry in next packet
        }
        common::LOG_ERROR("stream send data failed. stream id:%d, frame type:%d", stream_id_, frame->GetType());
        return TrySendResult::kFailed;
    }

    // Update state machine only after successful send
    if (!send_machine_->OnFrame(frame->GetType())) {
        common::LOG_WARN("stream state transition rejected. stream id:%d, frame type:%d, state=%d", stream_id_,
            frame->GetType(), static_cast<int>(send_machine_->GetStatus()));
    }

    // Mark FIN as sent only after successful transmission
    if (has_fin) {
        fin_sent_ = true;
    }
    visitor->AddStreamDataSize(frame->GetData().GetLength());
    common::LOG_DEBUG("stream send data. stream id:%d, send size:%d,is fin:%d, left data size:%d", stream_id_,
        frame->GetData().GetLength(), frame->IsFin(), send_buffer_->GetDataLength());

    send_buffer_->MoveReadPt(frame->GetData().GetLength());
    send_data_offset_ += frame->GetData().GetLength();

    common::LOG_DEBUG("stream send data callback. stream id:%d, send size:%d,is fin:%d", stream_id_,
        frame->GetData().GetLength(), frame->IsFin());
    if (sended_cb_) {
        common::LOG_DEBUG("stream send data callback. stream id:%d, send size:%d,is fin:%d", stream_id_,
            frame->GetData().GetLength(), frame->IsFin());
        sended_cb_(frame->GetData().GetLength(), 0);
    }

    // Metrics: Stream data sent
    common::Metrics::CounterInc(common::MetricsStd::QuicStreamsBytesTx, frame->GetData().GetLength());

    // if there is still data in the buffer, signal that more packets are needed
    if (send_buffer_->GetDataLength() > 0) {
        // Still have data to send - return kBreak to stay in active list
        // and trigger immediate sending of next packet
        return TrySendResult::kBreak;
    }
    return TrySendResult::kSuccess;
}

void SendStream::OnMaxStreamDataFrame(std::shared_ptr<IFrame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxStreamDataFrame>(frame);
    uint64_t new_limit = max_data_frame->GetMaximumData();

    if (new_limit <= peer_data_limit_) {
        common::LOG_WARN("get a invalid max data stream. new limit:%llu, current limit:%llu", new_limit, peer_data_limit_);
        return;
    }

    // BUGFIX P1-4: Removed unused can_write_size variable that also had
    // uint64_t → uint32_t truncation (new_limit and peer_data_limit_ are uint64_t)
    peer_data_limit_ = new_limit;

    if (send_buffer_->GetDataLength() > 0) {
        ToSend();
    }
    common::LOG_DEBUG("stream recv max stream data. stream id:%llu, new limit:%llu", stream_id_, new_limit);
}

void SendStream::OnStopSendingFrame(std::shared_ptr<IFrame> frame) {
    auto stop_frame = std::dynamic_pointer_cast<StopSendingFrame>(frame);
    uint32_t err = stop_frame->GetAppErrorCode();

    // RFC 9000 Section 3.5: An endpoint that receives a STOP_SENDING frame MUST send a RESET_STREAM frame
    // if the stream is in the "Ready" or "Send" state.
    // RFC 9000 Section 3.5: If the stream is in the "Data Sent" state, the endpoint MAY defer
    // sending the RESET_STREAM frame until the packets containing outstanding data are acknowledged or declared lost.
    //
    // For HTTP/3 request streams: when the client has already sent FIN (Data Sent state), sending
    // RESET_STREAM is harmful because some implementations (e.g., quic-go) interpret it as "request cancelled"
    // and stop sending the response. We defer RESET_STREAM when in Data Sent state.
    auto current_state = send_machine_->GetStatus();
    if (current_state == StreamState::kReady || current_state == StreamState::kSend) {
        // MUST send RESET_STREAM in Ready or Send state
        // NOTE: Explicitly call SendStream::Reset (not the virtual Reset) to only reset the send direction.
        // For BidirectionStream, the virtual Reset() would also reset the recv direction, which is wrong here.
        SendStream::Reset(err);
    }
    // In Data Sent state: defer RESET_STREAM (RFC 9000 allows this)
    // The data is already sent with FIN; peer already knows the final size.

    if (sended_cb_) {
        sended_cb_(0, err);
    }
    common::LOG_DEBUG("stream recv stop sending. stream id:%d, error:%d", stream_id_, err);
}

void SendStream::OnDataAcked(uint64_t offset_start, uint64_t length, bool has_fin) {
    common::LOG_DEBUG(
        "SendStream::OnDataAcked: stream_id=%d, range=[%llu,%llu), has_fin=%d, current acked_offset=%llu, "
        "send_data_offset=%llu",
        stream_id_, offset_start, offset_start + length, has_fin, acked_offset_, send_data_offset_);

    // 1. Insert [offset_start, offset_start + length) into the disjoint
    //    interval set, merging any neighbours we touch. A length of 0 is
    //    legal (e.g. a FIN-only frame) — we just skip the range insert.
    if (length > 0) {
        uint64_t new_start = offset_start;
        uint64_t new_end = offset_start + length;

        // Find the first range whose start > new_start, then step back if the
        // preceding range overlaps/abuts. This collapses any chain of ranges
        // that the new interval bridges in one pass.
        auto it = acked_ranges_.upper_bound(new_start);
        if (it != acked_ranges_.begin()) {
            auto prev = std::prev(it);
            if (prev->second >= new_start) {
                new_start = prev->first;
                if (prev->second > new_end) new_end = prev->second;
                it = prev;
            }
        }
        while (it != acked_ranges_.end() && it->first <= new_end) {
            if (it->second > new_end) new_end = it->second;
            it = acked_ranges_.erase(it);
        }
        acked_ranges_.emplace(new_start, new_end);
    }

    // 2. Recompute the contiguous ACKed prefix length. acked_offset_ is the
    //    largest N such that [0, N) is fully covered. With a sorted map this
    //    is just the end of the first range, *iff* it starts at 0.
    if (!acked_ranges_.empty()) {
        auto first = acked_ranges_.begin();
        if (first->first == 0 && first->second > acked_offset_) {
            acked_offset_ = first->second;
        }
    }

    // 3. Track FIN ACK separately. The flag is also set in TrySendData when
    //    we *put* a FIN onto the wire — keep both writers idempotent. Tagging
    //    on the ACK path is still important because retransmission may
    //    eventually be the only path to deliver the FIN, and we want
    //    AllAckDone to fire on the ACK rather than the original send.
    if (has_fin) {
        fin_sent_ = true;
    }

    // 4. Stream completion: every sent byte must be inside acked_ranges_ AND
    //    FIN must have been ACKed. The first condition collapses to
    //    acked_offset_ >= send_data_offset_ because acked_ranges_ is disjoint
    //    and the contiguous prefix anchored at 0 is exactly acked_offset_.
    CheckAllDataAcked();
}

void SendStream::CheckAllDataAcked() {
    // All bytes [0, send_data_offset_) must be covered by the contiguous
    // ACKed prefix, AND the FIN we wrote on the wire must have been ACKed.
    if (fin_sent_ && acked_offset_ >= send_data_offset_) {
        common::LOG_DEBUG(
            "SendStream::CheckAllDataAcked: all data acked for stream %d, transitioning to Data Recvd state "
            "(acked=%llu, sent=%llu, ranges=%zu)",
            stream_id_, acked_offset_, send_data_offset_, acked_ranges_.size());

        // Transition to terminal state (Data Recvd)
        if (send_machine_->AllAckDone()) {
            // For bidirectional streams, this will be handled by CheckStreamClose
            // For unidirectional send streams, the stream can be closed now
            common::LOG_DEBUG("SendStream: stream %d reached Data Recvd state", stream_id_);
        }
    }
}

}  // namespace quic
}  // namespace quicx
