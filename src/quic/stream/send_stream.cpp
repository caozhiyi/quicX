#include <algorithm>
#include <cstdint>
#include <quicx/common/metrics.h>

#include "common/log/log.h"
#include "common/metrics/metrics_std.h"

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
    fin_acked_(false),
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

    LOG_DEBUG("SendStream::Close: stream id:%llu", stream_id_);
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

    // RFC 9000 §3.1: once RESET_STREAM is sent, STREAM data is abandoned —
    // lost ranges must not be re-emitted.
    retx_queue_.clear();

    auto frame = std::make_shared<ResetStreamFrame>();
    frame->SetStreamID(stream_id_);
    frame->SetFinalSize(send_data_offset_);
    frame->SetAppErrorCode(error);
    LOG_DEBUG("stream send reset stream. stream id:%llu, error:%u", stream_id_, error);
    // RESET_STREAM is how the peer learns the final size and the abort: it
    // must survive a lost packet (RFC 9000 3.1: retransmitted until acked).
    TrackControlFrame(frame);
    frames_list_.emplace_back(frame);
    ToSend();

    // Metrics: RESET_STREAM sent
    Metrics::CounterInc(common::MetricsStd::QuicStreamsResetTx);
}

int32_t SendStream::Send(uint8_t* data, uint32_t len) {
    auto loop = event_loop_.lock();
    if (!loop) return -1;
    LOG_DEBUG("SendStream::Send called: stream_id=%llu, len=%u, IsInLoopThread=%d", GetStreamID(), len,
        loop->IsInLoopThread());

    if (!loop->IsInLoopThread()) {
        LOG_WARN("SendStream::Send called from wrong thread, posting to EventLoop: stream_id=%llu, len=%u",
            GetStreamID(), len);
        std::vector<uint8_t> vec(data, data + len);
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, vec = std::move(vec)]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<SendStream>(self);
            if (stream) {
                LOG_DEBUG("SendStream::Send async execution in EventLoop: stream_id=%llu, len=%zu",
                    stream->GetStreamID(), vec.size());
                stream->Send(const_cast<uint8_t*>(vec.data()), vec.size());
            }
        });
        return len;
    }

    if (!send_machine_->CheckCanSendFrame(FrameType::kStream)) {
        LOG_DEBUG("stream send flush failed, the state can't send stream frame. stream id:%llu,", stream_id_);
        return -1;
    }

    int32_t ret = send_buffer_->Write(data, len);
    LOG_DEBUG("SendStream::Send: wrote to buffer, stream_id=%llu, requested=%u, written=%d, buffer_size=%u",
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
        LOG_DEBUG("stream send flush failed, the state can't send stream frame. stream id:%llu,", stream_id_);
        return false;
    }

    if (send_buffer_->GetDataLength() == 0) {
        LOG_DEBUG(
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
            LOG_ERROR("unexpected frame on send stream. frame type:%d", frame_type);
    }
    return 0;
}

IStream::TrySendResult SendStream::TrySendData(IFrameVisitor* visitor, EncryptionLevel level) {
    IStream::TrySendData(nullptr, level);

    // Stream-level control frames (RESET_STREAM, and the STREAM_DATA_BLOCKED /
    // RESET_STREAM copies TrackControlFrame re-queues) go out BEFORE any
    // flow-control gate. They carry no stream payload, so the peer's
    // MAX_STREAM_DATA has nothing to do with them -- and a stream that is
    // flow-control blocked is exactly the one an application is most likely to
    // abandon with Reset(). With the old order the RESET_STREAM sat behind the
    // `peer_data_limit_ <= send_data_offset_` return and the reliable-delivery
    // work for it was moot.
    //
    // Packet full is NOT a failure: StreamManager treats kFailed as a permanent
    // error and drops the stream from the active list, which would strand the
    // frame in frames_list_ with nobody left to wake the stream. Return kBreak
    // (same as RecvStream::TrySendData) so the stream stays active and the
    // frame leaves with the next packet.
    for (auto iter = frames_list_.begin(); iter != frames_list_.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = frames_list_.erase(iter);
            continue;
        }
        if (visitor->GetLastError() == FrameEncodeError::kInsufficientSpace) {
            LOG_DEBUG("stream send: packet full while sending control frame, retry. stream id:%llu", stream_id_);
            return TrySendResult::kBreak;
        }
        LOG_DEBUG("stream send data failed. stream id:%d, frame type:%d", stream_id_, (*iter)->GetType());
        return TrySendResult::kFailed;
    }

    // P0-C10 / D2: re-emit lost / probed ranges. They sit below
    // send_data_offset_ (already admitted by both flow controllers), so they
    // must bypass the peer-limit gate below, and they must stay sendable in
    // kDataSent (FIN already out, not yet acknowledged), which the
    // CheckCanSendFrame(kStream) gate further down forbids.
    if (!retx_queue_.empty()) {
        const StreamState st = send_machine_->GetStatus();
        if (st == StreamState::kResetSent || st == StreamState::kResetRecvd || st == StreamState::kDataRecvd) {
            retx_queue_.clear();
        } else {
            auto retx_result = DrainRetransmit(visitor);
            if (retx_result != TrySendResult::kSuccess) {
                return retx_result;
            }
        }
    }

    // check peer limit (guard against unsigned underflow when send_data_offset_ > peer_data_limit_)
    if (peer_data_limit_ > send_data_offset_ && peer_data_limit_ - send_data_offset_ < kStreamDataBlockedThreshold) {
        // Only send STREAM_DATA_BLOCKED once per limit value
        // When peer sends MAX_STREAM_DATA with new limit, blocked_at_limit_ will be less than new peer_data_limit_
        if (blocked_at_limit_ != peer_data_limit_ && send_machine_->CheckCanSendFrame(FrameType::kStreamDataBlocked)) {
            // make stream block frame
            std::shared_ptr<StreamDataBlockedFrame> frame = std::make_shared<StreamDataBlockedFrame>();
            frame->SetStreamID(stream_id_);
            frame->SetMaximumData(peer_data_limit_);
            LOG_DEBUG("stream send data blocked. stream id:%llu, peer data limit:%llu", stream_id_, peer_data_limit_);

            // Metrics: Stream blocked by flow control
            Metrics::CounterInc(common::MetricsStd::QuicStreamDataBlocked);

            // blocked_at_limit_ de-duplicates per limit, so a lost frame is
            // never minted again: give it an owner. Re-send only while the
            // limit it reported is still the current one.
            const uint64_t reported_limit = peer_data_limit_;
            TrackControlFrame(frame, [this, reported_limit]() { return peer_data_limit_ == reported_limit; });

            if (!visitor->HandleFrame(frame)) {
                LOG_DEBUG("stream send data blocked failed. stream id:%d, frame type:%d", stream_id_, frame->GetType());
                return TrySendResult::kFailed;
            }
            // Mark that we've sent STREAM_DATA_BLOCKED for this limit
            blocked_at_limit_ = peer_data_limit_;
        }
    }

    if (peer_data_limit_ <= send_data_offset_) {
        LOG_DEBUG("stream send data flow control blocked. stream id:%llu, peer data limit:%llu, send data offset:%llu",
            stream_id_, peer_data_limit_, send_data_offset_);
        return TrySendResult::kFlowControlBlocked;  // Keep in active list, waiting for MAX_STREAM_DATA
    }

    if (!send_machine_->CheckCanSendFrame(FrameType::kStream)) {
        LOG_DEBUG("stream send data success. stream id:%d, can send stream frame:%d", stream_id_,
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
        uint64_t stream_send_size_64 =
            (peer_data_limit_ > send_data_offset_) ? (peer_data_limit_ - send_data_offset_) : 0;
        uint32_t stream_send_size =
            static_cast<uint32_t>(std::min(stream_send_size_64, static_cast<uint64_t>(UINT32_MAX)));
        uint32_t conn_send_size = visitor->GetLeftStreamDataSize();
        send_size = stream_send_size > conn_send_size ? conn_send_size : stream_send_size;

        // Per-datagram cap: the STREAM frame must fit into whatever space
        // the current packet buffer still has. Reserve a small budget for
        // the STREAM header (worst case: type<=2B + stream_id<=8B + offset
        // <=8B + length<=2B = 20B; round to 24B for safety). This replaces
        // the historical hardcoded 1300 cap which was ~120B smaller than
        // the visitor's real budget (kMaxFramePayload=1420), causing each
        // 1500B send_buffer chunk to be split into 1300+200 fragments and
        // halving steady-state datagram payload.
        constexpr uint32_t kStreamHeaderReserve = 24;
        uint32_t pkt_left = visitor->GetPacketLeftSize();
        uint32_t pkt_cap = pkt_left > kStreamHeaderReserve ? pkt_left - kStreamHeaderReserve : 0;
        if (send_size > pkt_cap) {
            send_size = pkt_cap;
        }

        // Diagnostic (datagram fill): record the four budget components
        Metrics::CounterInc(common::MetricsStd::DiagStreamSlackSum, stream_send_size);
        Metrics::CounterInc(common::MetricsStd::DiagVisitorLeftSum, conn_send_size);
        Metrics::HistogramObserve(common::MetricsStd::DiagSendSizeHist, send_size);

        // Diagnostic (send_buffer state): chunk fragmentation snapshot.
        Metrics::CounterInc(common::MetricsStd::DiagSendBufTotalSum, send_buffer_->GetDataLength());
        Metrics::CounterInc(common::MetricsStd::DiagSendBufChunksSum, send_buffer_->GetChunkCount());
        Metrics::CounterInc(common::MetricsStd::DiagSendBufProbeCount);

        LOG_DEBUG(
            "stream send calc: stream_id:%d, peer_limit:%llu, send_offset:%llu, stream_send_size:%u, "
            "conn_send_size:%u, final_send_size:%u",
            stream_id_, peer_data_limit_, send_data_offset_, stream_send_size, conn_send_size, send_size);

        // Only try to get data if we have space to send
        if (send_size > 0) {
            // Coalesce-on-demand: prefer a single contiguous span up to
            // send_size bytes. If the first chunk is partially drained
            // (typical: 1500B chunk with 1234B already consumed leaves
            // 266B), GetCoalescedReadable will stitch the tail with the
            // head of the next chunk via a single bounded memcpy. This
            // breaks the (1500B chunk size) vs (~1396B per-packet payload)
            // mismatch that otherwise produces the steady-state bimodal
            // pkt_payload distribution (half packets ≥1024B, half <256B).
            //
            // Fast path (zero-copy) still applies when the first chunk
            // alone has enough bytes — equivalent to the old
            // GetFirstChunkReadable behaviour.
            //
            // The downstream encoder (StreamFrame::Encode → EncodeBytes)
            // already memcpy's the payload into the packet buffer, so the
            // extra coalesce memcpy at most doubles a constant-size copy
            // on a minority of iterations; it never adds an O(N) pass.
            common::SharedBufferSpan data = send_buffer_->GetCoalescedReadable(send_size);
            if (data.Valid()) {
                frame->SetData(data);
                Metrics::CounterInc(common::MetricsStd::DiagFirstChunkSum, data.GetLength());
                Metrics::CounterInc(common::MetricsStd::DiagStreamSendSizeSum, data.GetLength());
                Metrics::CounterInc(common::MetricsStd::DiagStreamSendCount);
                Metrics::HistogramObserve(common::MetricsStd::DiagFirstChunkHist, data.GetLength());
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
            uint64_t stream_slack = (peer_data_limit_ > send_data_offset_) ? (peer_data_limit_ - send_data_offset_) : 0;
            bool stream_level_blocked = stream_slack < kStreamDataBlockedThreshold;

            LOG_DEBUG(
                "stream send data: zero-data frame. stream id:%d, buffer_len:%d, "
                "peer_limit:%llu, send_offset:%llu, stream_slack:%llu, stream_level_blocked:%d",
                stream_id_, send_buffer_->GetDataLength(), peer_data_limit_, send_data_offset_, stream_slack,
                stream_level_blocked ? 1 : 0);

            if (stream_level_blocked) {
                // Genuine stream-level FC. RFC 9000 §19.13: STREAM_DATA_BLOCKED
                // is informational; de-dup with blocked_at_limit_ so we never
                // emit twice at the same limit.
                if (blocked_at_limit_ != peer_data_limit_ &&
                    send_machine_->CheckCanSendFrame(FrameType::kStreamDataBlocked)) {
                    auto blocked_frame = std::make_shared<StreamDataBlockedFrame>();
                    blocked_frame->SetStreamID(stream_id_);
                    blocked_frame->SetMaximumData(peer_data_limit_);
                    const uint64_t reported_limit = peer_data_limit_;
                    TrackControlFrame(
                        blocked_frame, [this, reported_limit]() { return peer_data_limit_ == reported_limit; });
                    visitor->HandleFrame(blocked_frame);
                    blocked_at_limit_ = peer_data_limit_;
                    LOG_DEBUG("stream send: sent STREAM_DATA_BLOCKED frame. stream id:%d, limit:%llu", stream_id_,
                        peer_data_limit_);
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
        LOG_DEBUG("stream send data: no data to send. stream id:%d", stream_id_);
        return TrySendResult::kSuccess;
    }

    AttachLossHandler(frame);

    // Try to send the frame first, before updating state machine
    if (!visitor->HandleFrame(frame)) {
        // Check if the failure was due to insufficient packet space
        if (visitor->GetLastError() == FrameEncodeError::kInsufficientSpace) {
            LOG_INFO(
                "stream send data: packet full, need retry. stream id:%d, frame type:%d", stream_id_, frame->GetType());
            return TrySendResult::kBreak;  // Packet is full, stream needs to retry in next packet
        }
        LOG_ERROR("stream send data failed. stream id:%d, frame type:%d", stream_id_, frame->GetType());
        return TrySendResult::kFailed;
    }

    // Update state machine only after successful send
    if (!send_machine_->OnFrame(frame->GetType())) {
        LOG_WARN("stream state transition rejected. stream id:%d, frame type:%d, state=%d", stream_id_,
            frame->GetType(), static_cast<int>(send_machine_->GetStatus()));
    }

    // Mark FIN as sent only after successful transmission
    if (has_fin) {
        fin_sent_ = true;
    }
    visitor->AddStreamDataSize(frame->GetData().GetLength());
    LOG_DEBUG("stream send data. stream id:%d, send size:%d,is fin:%d, left data size:%d", stream_id_,
        frame->GetData().GetLength(), frame->IsFin(), send_buffer_->GetDataLength());

    send_buffer_->MoveReadPt(frame->GetData().GetLength());
    send_data_offset_ += frame->GetData().GetLength();

    LOG_DEBUG("stream send data callback. stream id:%d, send size:%d,is fin:%d", stream_id_,
        frame->GetData().GetLength(), frame->IsFin());
    if (sended_cb_) {
        LOG_DEBUG("stream send data callback. stream id:%d, send size:%d,is fin:%d", stream_id_,
            frame->GetData().GetLength(), frame->IsFin());
        sended_cb_(frame->GetData().GetLength(), 0);
    }

    // Metrics: Stream data sent
    Metrics::CounterInc(common::MetricsStd::QuicStreamsBytesTx, frame->GetData().GetLength());

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
        LOG_WARN("get a invalid max data stream. new limit:%llu, current limit:%llu", new_limit, peer_data_limit_);
        return;
    }

    // BUGFIX P1-4: Removed unused can_write_size variable that also had
    // uint64_t → uint32_t truncation (new_limit and peer_data_limit_ are uint64_t)
    peer_data_limit_ = new_limit;

    if (send_buffer_->GetDataLength() > 0) {
        ToSend();
    }
    LOG_DEBUG("stream recv max stream data. stream id:%llu, new limit:%llu", stream_id_, new_limit);
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
    LOG_DEBUG("stream recv stop sending. stream id:%d, error:%d", stream_id_, err);
}

void SendStream::OnDataAcked(uint64_t offset_start, uint64_t length, bool has_fin) {
    LOG_DEBUG(
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
        fin_acked_ = true;
    }

    // 3b. D2: drop queued retransmissions the ACK just made redundant (late
    //     ACK of a packet already declared lost). Only whole segments inside
    //     the contiguous prefix are pruned here; partial overlaps are trimmed
    //     lazily at emission time (DrainRetransmit).
    for (auto it = retx_queue_.begin(); it != retx_queue_.end() && it->first <= acked_offset_;) {
        const bool fin_pending = it->second.fin && !fin_acked_;
        if (it->second.end <= acked_offset_ && !fin_pending) {
            it = retx_queue_.erase(it);
        } else {
            ++it;
        }
    }

    // 4. Stream completion: every sent byte must be inside acked_ranges_ AND
    //    FIN must have been ACKed. The first condition collapses to
    //    acked_offset_ >= send_data_offset_ because acked_ranges_ is disjoint
    //    and the contiguous prefix anchored at 0 is exactly acked_offset_.
    CheckAllDataAcked();
}

void SendStream::CheckAllDataAcked() {
    // All bytes [0, send_data_offset_) must be covered by the contiguous
    // ACKed prefix, AND the FIN must have been ACKed. With the whole-packet
    // retransmission channel retired, "FIN written to the wire" (fin_sent_)
    // is no longer enough: a lost FIN-only frame must stay retransmittable,
    // so the stream may only reach Data Recvd once a FIN-carrying frame is
    // actually acknowledged.
    if (fin_sent_ && fin_acked_ && acked_offset_ >= send_data_offset_) {
        LOG_DEBUG(
            "SendStream::CheckAllDataAcked: all data acked for stream %d, transitioning to Data Recvd state "
            "(acked=%llu, sent=%llu, ranges=%zu)",
            stream_id_, acked_offset_, send_data_offset_, acked_ranges_.size());

        // Transition to terminal state (Data Recvd)
        if (send_machine_->AllAckDone()) {
            // For bidirectional streams, this will be handled by CheckStreamClose
            // For unidirectional send streams, the stream can be closed now
            LOG_DEBUG("SendStream: stream %d reached Data Recvd state", stream_id_);
        }
    }
}

// ==================== P0-C10 / D2: frame-level STREAM retransmission ====================

namespace {

// Zero-copy sub-slice [skip, skip + len) of `s`; empty span when len == 0.
common::SharedBufferSpan SliceSpan(const common::SharedBufferSpan& s, uint64_t skip, uint64_t len) {
    if (!s.Valid() || len == 0) {
        return common::SharedBufferSpan();
    }
    return common::SharedBufferSpan(s.GetChunk(), s.GetStart() + skip, static_cast<uint32_t>(len));
}

}  // namespace

void SendStream::AttachLossHandler(const std::shared_ptr<StreamFrame>& frame) {
    // The handler lives inside the frame and is only ever invoked through
    // frame->NotifyDelivery(), so a raw pointer to the frame is always valid
    // while the handler runs (and avoids a frame<->handler ownership cycle).
    std::weak_ptr<IStream> weak_self = weak_from_this();
    StreamFrame* raw = frame.get();
    frame->SetDeliveryHandler([weak_self, raw](FrameDeliveryState state) {
        if (state == FrameDeliveryState::kAcked) {
            return;  // ACKs reach OnDataAcked through StreamDataInfo
        }
        auto self = std::dynamic_pointer_cast<SendStream>(weak_self.lock());
        if (self) {
            self->OnFrameLost(raw->GetOffset(), raw->GetData(), raw->IsFin());
        }
    });
}

std::vector<std::pair<uint64_t, uint64_t>> SendStream::UncoveredGaps(uint64_t start, uint64_t end) const {
    std::vector<std::pair<uint64_t, uint64_t>> covered;
    auto collect = [&](const auto& m, auto end_of) {
        auto it = m.upper_bound(start);
        if (it != m.begin()) {
            --it;  // the predecessor may straddle `start`
        }
        for (; it != m.end() && it->first < end; ++it) {
            const uint64_t s = it->first;
            const uint64_t e = end_of(it->second);
            if (e > s && e > start) {
                covered.emplace_back(s, e);
            }
        }
    };
    collect(acked_ranges_, [](uint64_t v) { return v; });
    collect(retx_queue_, [](const RetxSegment& r) { return r.end; });
    std::sort(covered.begin(), covered.end());

    std::vector<std::pair<uint64_t, uint64_t>> gaps;
    uint64_t cur = start;
    for (const auto& c : covered) {
        if (c.first >= end || cur >= end) {
            break;
        }
        if (c.first > cur) {
            gaps.emplace_back(cur, c.first);
        }
        if (c.second > cur) {
            cur = c.second;
        }
    }
    if (cur < end) {
        gaps.emplace_back(cur, end);
    }
    return gaps;
}

void SendStream::OnFrameLost(uint64_t offset, const common::SharedBufferSpan& data, bool fin) {
    const StreamState st = send_machine_->GetStatus();
    if (st == StreamState::kResetSent || st == StreamState::kResetRecvd || st == StreamState::kDataRecvd) {
        return;  // abandoned (RESET_STREAM) or fully acknowledged: nothing to retransmit
    }

    const uint64_t len = data.Valid() ? data.GetLength() : 0;
    const uint64_t end = offset + len;
    const bool want_fin = fin && !fin_acked_;

    // Re-queue only the parts of [offset, end) that are neither ACKed nor
    // already queued. A fully-covered range yields no gaps, which makes this
    // idempotent under repeated kProbe (and a late kLost after a late ACK).
    auto gaps = UncoveredGaps(offset, end);
    for (const auto& gap : gaps) {
        const bool gap_fin = want_fin && gap.second == end;
        retx_queue_.emplace(
            gap.first, RetxSegment{gap.second, SliceSpan(data, gap.first - offset, gap.second - gap.first), gap_fin});
    }

    // FIN not carried by a fresh gap (tail already ACKed or already queued, or a
    // FIN-only frame): make sure some queued entry will still deliver it.
    if (want_fin && !(len > 0 && !gaps.empty() && gaps.back().second == end)) {
        auto it = retx_queue_.upper_bound(end);
        bool attached = false;
        if (it != retx_queue_.begin()) {
            auto prev = std::prev(it);
            if (prev->second.end == end) {
                prev->second.fin = true;  // also covers an existing FIN-only entry keyed at `end`
                attached = true;
            }
        }
        if (!attached) {
            retx_queue_.emplace(end, RetxSegment{end, common::SharedBufferSpan(), true});
        }
    }

    if (retx_queue_.empty()) {
        return;
    }
    LOG_DEBUG("SendStream::OnFrameLost: stream=%llu range=[%llu,%llu) fin=%d gaps=%zu queued_segments=%zu",
        (unsigned long long)stream_id_, (unsigned long long)offset, (unsigned long long)end, fin ? 1 : 0, gaps.size(),
        retx_queue_.size());
    ToSend();
}

IStream::TrySendResult SendStream::DrainRetransmit(IFrameVisitor* visitor) {
    // Same worst-case STREAM header budget as the new-data path.
    constexpr uint32_t kStreamHeaderReserve = 24;

    while (!retx_queue_.empty()) {
        auto it = retx_queue_.begin();
        const uint64_t start = it->first;
        RetxSegment& seg = it->second;

        // A FIN acknowledged since queueing makes the FIN flag redundant; a
        // segment left with neither bytes nor FIN is dead.
        if (fin_acked_) {
            seg.fin = false;
        }
        if (seg.end <= start && !seg.fin) {
            retx_queue_.erase(it);
            continue;
        }

        // Trim a front already covered by a (late) ACK.
        uint64_t new_start = start;
        auto acked = acked_ranges_.upper_bound(start);
        if (acked != acked_ranges_.begin()) {
            auto prev = std::prev(acked);
            if (prev->second > start) {
                new_start = std::min<uint64_t>(prev->second, seg.end);
            }
        }
        if (new_start > start) {
            RetxSegment trimmed{seg.end, SliceSpan(seg.data, new_start - start, seg.end - new_start), seg.fin};
            retx_queue_.erase(it);
            retx_queue_.emplace(new_start, std::move(trimmed));
            continue;
        }

        const uint64_t remaining = seg.end - start;

        // An ACK that arrived AFTER this segment was queued can land in its
        // middle (out-of-order delivery): the queue-time gap computation could
        // not know. Stop this emission at the next acknowledged range instead
        // of re-sending bytes the peer already has; the tail beyond it is
        // trimmed by the front-trim above on the next iteration. (Front-only
        // trimming left a redundant-byte window of up to one segment.)
        uint64_t sendable = remaining;
        auto next_acked = acked_ranges_.upper_bound(start);
        if (next_acked != acked_ranges_.end() && next_acked->first < seg.end) {
            sendable = next_acked->first - start;
        }

        const uint32_t pkt_left = visitor->GetPacketLeftSize();
        const uint32_t cap = pkt_left > kStreamHeaderReserve ? pkt_left - kStreamHeaderReserve : 0;
        if (cap == 0) {
            return TrySendResult::kBreak;  // packet full; stay active for the next one
        }
        const uint32_t send_size = static_cast<uint32_t>(std::min<uint64_t>(sendable, cap));
        // FIN rides only on the segment's true tail, never on a piece cut short
        // by an acknowledged range.
        const bool send_fin = seg.fin && send_size == remaining;

        auto frame = std::make_shared<StreamFrame>();
        frame->SetStreamID(stream_id_);
        frame->SetOffset(start);
        if (send_size > 0) {
            frame->SetData(SliceSpan(seg.data, 0, send_size));
        }
        if (send_fin) {
            frame->SetFin();
        }
        AttachLossHandler(frame);

        if (!visitor->HandleFrame(frame)) {
            if (visitor->GetLastError() == FrameEncodeError::kInsufficientSpace) {
                return TrySendResult::kBreak;
            }
            LOG_ERROR("SendStream::DrainRetransmit: encode failed. stream id:%llu, offset:%llu, size:%u",
                (unsigned long long)stream_id_, (unsigned long long)start, send_size);
            return TrySendResult::kFailed;
        }

        // Consume what was just emitted. No state-machine transition (the
        // original send already made it), no AddStreamDataSize (connection
        // flow control was charged on first transmission), no sended_cb_
        // (the application was told when the bytes first left).
        if (send_size == remaining) {
            retx_queue_.erase(it);
        } else {
            RetxSegment rest{seg.end, SliceSpan(seg.data, send_size, remaining - send_size), seg.fin};
            retx_queue_.erase(it);
            retx_queue_.emplace(start + send_size, std::move(rest));
        }
        LOG_DEBUG("SendStream::DrainRetransmit: stream=%llu re-sent [%llu,%llu) fin=%d queued_segments=%zu",
            (unsigned long long)stream_id_, (unsigned long long)start, (unsigned long long)(start + send_size),
            send_fin ? 1 : 0, retx_queue_.size());
    }
    return TrySendResult::kSuccess;
}

}  // namespace quic
}  // namespace quicx
