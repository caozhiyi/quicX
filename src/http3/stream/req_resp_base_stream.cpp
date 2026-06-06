#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"
#include "common/qlog/qlog.h"

#include "quic/quicx/global_resource.h"

#include "http3/config.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/frame/type.h"
#include "http3/http/error.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/stream/req_resp_base_stream.h"

namespace quicx {
namespace http3 {

ReqRespBaseStream::ReqRespBaseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackEncoder>& qpack_decoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<IQuicBidirectionStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IStream(StreamType::kReqResp, error_handler),
    is_last_data_(false),
    current_frame_is_last_(false),
    qpack_encoder_(qpack_encoder),
    qpack_decoder_(qpack_decoder),
    blocked_registry_(blocked_registry),
    is_provider_mode_(false),
    all_provider_data_sent_(false),
    stream_(stream) {
    // Callback registration moved to Init() method
    // Cannot call shared_from_this() here because object is not yet managed by shared_ptr
}

ReqRespBaseStream::~ReqRespBaseStream() {
    // Do NOT call Close() here - it should have been called after sending the
    // last frame Calling Close() in destructor may cause issues if the stream is
    // already closed or if the stream state is not suitable for closing
}

void ReqRespBaseStream::SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) {
    qlog_trace_ = trace;
    // Propagate to frame decoder for http3:frame_parsed events
    frame_decoder_.SetQlogTrace(trace);
    frame_decoder_.SetStreamId(GetStreamID());
}

void ReqRespBaseStream::Init() {
    // Use weak_ptr to prevent use-after-free when callbacks are invoked after stream destruction
    auto weak_self = std::weak_ptr<ReqRespBaseStream>(shared_from_this());
    stream_->SetStreamReadCallBack([weak_self](std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
        if (auto self = weak_self.lock()) {
            self->OnData(data, is_last, error);
        }
    });
    stream_->SetStreamWriteCallBack([weak_self](uint32_t length, uint32_t error) {
        if (auto self = weak_self.lock()) {
            self->HandleSent(length, error);
        }
    });
}

void ReqRespBaseStream::OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
    if (error != 0) {
        LOG_ERROR("ReqRespBaseStream::OnData error: %d", error);
        if (error_handler_) {
            error_handler_(GetStreamID(), error);
        }
        return;
    }

    is_last_data_ = is_last;

    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(data);
    // If buffer is empty (e.g., FIN without data, or RESET_STREAM)
    if (data->GetDataLength() == 0) {
        if (is_last) {
            // FIN with no data: notify subclass to handle stream completion.
            // BUT: if the stream is currently QPACK-blocked, we must defer the
            // FIN as well, otherwise the subclass would observe end-of-stream
            // before the blocked HEADERS has been decoded and the application
            // would never see the headers/body that arrived behind them.
            if (is_currently_blocked_) {
                LOG_DEBUG(
                    "ReqRespBaseStream::OnData: FIN received while blocked, deferring until QPACK unblocks");
                pending_blocked_is_last_ = true;
                return;
            }
            LOG_DEBUG("ReqRespBaseStream::OnData: FIN with empty buffer, notifying end");
            HandleFinWithoutData();
        }
        // Check if we need to notify completion
        if (should_notify_completion_) {
            LOG_DEBUG("ReqRespBaseStream::OnData: notifying stream completion after empty data");
            if (error_handler_) {
                error_handler_(GetStreamID(), 0);
            }
            should_notify_completion_ = false;
        }
        return;
    }

    std::vector<std::shared_ptr<IFrame>> frames;
    if (!frame_decoder_.DecodeFrames(buffer, frames)) {
        LOG_ERROR("ReqRespBaseStream::OnData decode frames error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        }
        return;
    }

    LOG_DEBUG("ReqRespBaseStream::OnData: processing %zu frames, is_last_data=%d, currently_blocked=%d",
        frames.size(), is_last_data_, is_currently_blocked_);

    if (is_currently_blocked_) {
        // The stream is parked behind a still-blocked HEADERS frame at the
        // head of pending_blocked_frames_. Newly-arrived frames must be
        // queued in order — replaying out of order would deliver DATA
        // frames to the application before their owning header section
        // has been decoded. The FIN bit (if any) attaches to the new tail
        // because, by the time we drain, the ordered concatenation of
        // (already-queued frames) + (frames in this batch) is what was
        // logically on the wire.
        for (auto& f : frames) {
            pending_blocked_frames_.push_back(f);
        }
        pending_blocked_is_last_ = pending_blocked_is_last_ || is_last_data_;
        return;
    }

    ProcessFrames(frames, is_last_data_);

    // CRITICAL FIX: If we got FIN but no frames were decoded,
    // we need to notify via HandleFinWithoutData (implemented in subclass)
    if (is_last_data_ && frames.empty() && !is_currently_blocked_) {
        LOG_DEBUG("ReqRespBaseStream::OnData: FIN received but no frames decoded");
        HandleFinWithoutData();
    }

    // Process all frames first (including PUSH_PROMISE), then notify stream completion
    // This ensures PUSH_PROMISE frames are not ignored due to early stream cleanup.
    // Skip notification while we are still QPACK-blocked: the stream is not
    // actually done yet, and signalling completion here would tear down the
    // stream before the blocked HEADERS retry has a chance to fire.
    if (should_notify_completion_ && !is_currently_blocked_) {
        LOG_DEBUG("ReqRespBaseStream::OnData: notifying stream completion after processing all frames");
        if (error_handler_) {
            error_handler_(GetStreamID(), 0);
        }
        should_notify_completion_ = false;
    }
}

void ReqRespBaseStream::ProcessFrames(std::vector<std::shared_ptr<IFrame>>& frames, bool is_last_batch) {
    // Walk the frames in order. The trailing-frame is_last flag is only
    // meaningful for the very last frame we successfully dispatch in this
    // batch — earlier frames are not the end of the stream. If we trip
    // QPACK head-of-line blocking on a HEADERS frame somewhere in the
    // middle, the rest of this batch (along with that batch's FIN bit)
    // gets parked into pending_blocked_frames_ and replayed verbatim once
    // DrainPendingFrames() unblocks.
    for (size_t i = 0; i < frames.size(); ++i) {
        bool last_in_batch = is_last_batch && (i == frames.size() - 1);
        current_frame_is_last_ = last_in_batch;
        is_last_data_ = last_in_batch;
        LOG_DEBUG("ReqRespBaseStream::ProcessFrames: frame %zu/%zu, type=0x%x, is_last=%d",
            i + 1, frames.size(), static_cast<uint32_t>(frames[i]->GetType()), current_frame_is_last_);

        HandleFrame(frames[i]);

        // If HandleHeaders blocked on QPACK, stop dispatching here. The
        // remaining frames in this batch are stashed (in order) so the
        // retry callback can drain them. We also remember whether the
        // batch carried FIN, so the replay sees the same end-of-stream
        // signalling that the wire actually carried.
        if (is_currently_blocked_) {
            for (size_t j = i + 1; j < frames.size(); ++j) {
                pending_blocked_frames_.push_back(frames[j]);
            }
            pending_blocked_is_last_ = is_last_batch;
            LOG_DEBUG(
                "ReqRespBaseStream::ProcessFrames: QPACK blocked at frame %zu, parked %zu trailing frames (is_last=%d)",
                i, frames.size() - i - 1, pending_blocked_is_last_);
            return;
        }
    }
}

void ReqRespBaseStream::DrainPendingFrames() {
    // Called from the QPACK retry callback after the head HEADERS has
    // successfully decoded. The flag must already have been cleared by
    // HandleHeaders() before we get here.
    if (is_currently_blocked_) {
        // Defensive: should not happen, but if the head retry decoded one
        // section yet more inserts are still required by an intervening
        // queued HEADERS, we'll re-block during the loop below and leave
        // the remainder in place for the next IIC.
        return;
    }

    while (!pending_blocked_frames_.empty()) {
        auto frame = pending_blocked_frames_.front();
        pending_blocked_frames_.pop_front();

        // Determine is_last for this replayed frame:
        //   - true only when this is the very last queued frame AND the
        //     batch from which they came carried FIN.
        bool last_in_replay = pending_blocked_frames_.empty() && pending_blocked_is_last_;
        current_frame_is_last_ = last_in_replay;
        is_last_data_ = last_in_replay;

        HandleFrame(frame);

        if (is_currently_blocked_) {
            // Hit another blocked HEADERS during replay — the frame we
            // just dispatched was a HEADERS; HandleHeaders has already
            // re-pushed it to the head of the queue via the retry path
            // (see HandleHeaders). Stop here and wait for the next
            // unblock.
            return;
        }
    }

    // Everything drained. If the original batch carried FIN and there were
    // no DATA/HEADERS frames behind which to attach it (which would have
    // already taken care of the end-of-stream notification), surface FIN
    // now so the subclass can finish cleanly.
    bool was_fin = pending_blocked_is_last_;
    pending_blocked_is_last_ = false;
    if (was_fin && !should_notify_completion_) {
        // Mirror the empty-buffer FIN path in OnData(): if the subclass
        // hasn't already armed should_notify_completion_ in HandleData(),
        // we need to surface the end-of-stream now.
        HandleFinWithoutData();
    }
    if (should_notify_completion_) {
        if (error_handler_) {
            error_handler_(GetStreamID(), 0);
        }
        should_notify_completion_ = false;
    }
}

void ReqRespBaseStream::HandleHeaders(std::shared_ptr<IFrame> frame) {
    auto headers_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    if (!headers_frame) {
        LOG_ERROR("ReqRespBaseStream::HandleHeaders error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        }
        return;
    }

    // Frame-level completeness is already guaranteed by FrameDecoder /
    // HeadersFrame::Decode: a HEADERS frame is only emitted to us once the
    // full Length-bytes payload is in hand and confined to the declared
    // length. The remaining "is this header block well-formed?" question is
    // a QPACK-layer property, and is enforced below by qpack_decoder_->Decode
    // (which fails closed on truncated/blocked field sections). So no
    // separate length check is needed here.

    // Assign a real header-block-id = (stream_id << 32) | section_number
    if (header_block_key_ == 0) {
        uint64_t sid = GetStreamID();
        uint64_t secno = static_cast<uint64_t>(++next_section_number_);
        header_block_key_ = (sid << 32) | secno;
    }

    auto encoded_fields = headers_frame->GetEncodedFields();

    // RFC 9204 retry hazard: SingleBlockBuffer::CloneReadable advances the
    // *source* read pointer when it succeeds, but Decode also advances the
    // buffer's read pointer as it walks the prefix.  Once Decode returns
    // false partway through (RIC > InsertCount, or any other parse error),
    // the buffer's read pointer is already past the start of the field
    // section, and a naive retry would resume from that wrong offset and
    // either parse garbage or fail again with a misleading error.
    //
    // The fix is to take a non-consuming snapshot before the first attempt
    // (CloneReadable with move_write_pt=false) and feed that snapshot to
    // Decode/retry.  The snapshot shares the underlying chunk via
    // shared_ptr so it stays alive even after the original frame object is
    // dropped; each retry invocation can then itself re-snapshot from the
    // same template buffer.  We capture |encoded_fields_template| (a
    // shallow copy with its own read_pos_ aligned at the start of the
    // field section) by value into the retry lambda.
    auto encoded_fields_template = encoded_fields
        ? encoded_fields->CloneReadable(encoded_fields->GetDataLength(), /*move_write_pt=*/false)
        : nullptr;

    if (!encoded_fields_template) {
        LOG_ERROR("ReqRespBaseStream::HandleHeaders: encoded fields snapshot failed");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        return;
    }

    // First decode attempt. We feed a freshly-cloned view so the template
    // remains pristine for retries.
    auto first_attempt = encoded_fields_template->CloneReadable(
        encoded_fields_template->GetDataLength(), /*move_write_pt=*/false);
    if (!qpack_decoder_->Decode(first_attempt, headers_)) {
        // RFC 9204 §2.1.4: header section blocked on Required Insert Count.
        // Three correctness concerns:
        //   1. The retry must populate THIS stream's headers_ member —
        //      not a local temporary — so HandleHeaders() actually sees
        //      the decoded fields.  Previously the callback decoded into
        //      a throw-away `tmp` map, leaving headers_ empty on retry
        //      success and causing the application handler to receive a
        //      header-less request/response.
        //   2. NotifyAll() invokes EVERY pending retry callback whenever
        //      ANY new inserts arrive.  If our RIC still isn't met, the
        //      decode will fail again — and without re-Add, the entry
        //      would be silently dropped from the registry, causing the
        //      header block to be lost forever once the next IIC happens
        //      to land before our specific dependency is satisfied.  We
        //      re-Add ourselves with the same retry on persistent block.
        //   3. Set is_currently_blocked_ so the OnData/ProcessFrames
        //      caller stops pushing follow-on frames at us. Frames that
        //      arrive while blocked land in pending_blocked_frames_ and
        //      are replayed by DrainPendingFrames() once the head HEADERS
        //      finally decodes.
        is_currently_blocked_ = true;

        auto self_weak = std::weak_ptr<ReqRespBaseStream>(shared_from_this());
        blocked_retry_fn_ = [self_weak, encoded_fields_template]() {
            auto self = self_weak.lock();
            if (!self) {
                // Stream destroyed before unblock — nothing to do.
                return;
            }
            // Re-clone so each attempt sees a fresh read pointer at the
            // start of the field section (Decode will advance it).
            auto attempt = encoded_fields_template->CloneReadable(
                encoded_fields_template->GetDataLength(), /*move_write_pt=*/false);
            if (attempt && self->qpack_decoder_->Decode(attempt, self->headers_)) {
                // Success: clear the gate before invoking the application
                // hook and replaying any frames that were parked behind us.
                self->is_currently_blocked_ = false;

                // RFC 9204 §4.4.1: Only emit Section Ack when RIC > 0.
                if (self->qpack_decoder_->GetLastDecodedRequiredInsertCount() > 0) {
                    self->qpack_decoder_->EmitDecoderFeedback(
                        0x00, self->header_block_key_);
                }
                self->HandleHeaders();

                // Clear the retry callback to break any captures and free resources
                self->blocked_retry_fn_ = nullptr;

                // Now drain any DATA / further HEADERS that arrived behind
                // this section. DrainPendingFrames may itself re-block on a
                // later HEADERS in the queue (e.g., trailers that depend
                // on entries not yet inserted); in that case
                // is_currently_blocked_ flips back to true and we leave
                // the rest in the queue for the next IIC.
                self->DrainPendingFrames();

            } else {
                // Still blocked — re-enqueue with the same retry so the
                // next NotifyAll() picks us up again. Keep the gate set
                // so any frames arriving in the meantime continue to
                // queue rather than racing past us.
                self->blocked_registry_->Add(self->header_block_key_, self->blocked_retry_fn_);
            }
        };
        if (!blocked_registry_->Add(header_block_key_, blocked_retry_fn_)) {
            // RFC 9204 §5: peer exceeded its declared blocked-streams
            // capacity. Treat as a connection-level error per §2.1.2.
            LOG_ERROR(
                "ReqRespBaseStream::HandleHeaders: blocked_registry full (max blocked streams exceeded), "
                "stream=%llu key=%llu",
                static_cast<unsigned long long>(GetStreamID()),
                static_cast<unsigned long long>(header_block_key_));
            is_currently_blocked_ = false;
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kQpackDecompressionFailed);
            }
            return;
        }
        LOG_DEBUG("blocked header block key: %llu", header_block_key_);
        return;
    }
    // RFC 9204 §4.4.1: The decoder MUST NOT emit Section Acknowledgment for
    // header blocks that were processed without a dependency on the dynamic
    // table (i.e., with a Required Insert Count of 0). Emitting one causes
    // the peer to raise QPACK_DECODER_STREAM_ERROR because its encoder does
    // not track streams that only use static-table references.
    if (qpack_decoder_->GetLastDecodedRequiredInsertCount() > 0) {
        qpack_decoder_->EmitDecoderFeedback(0x00, header_block_key_);
    }

    HandleHeaders();
}

void ReqRespBaseStream::HandleData(std::shared_ptr<IFrame> frame) {
    auto data_frame = std::dynamic_pointer_cast<DataFrame>(frame);
    if (!data_frame) {
        LOG_ERROR("ReqRespBaseStream::HandleData error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        }
        return;
    }

    const auto& data = data_frame->GetData();

    // Use current_frame_is_last_ which is set per-frame in OnData
    // instead of is_last_data_ which would mark ALL frames as last
    HandleData(data, current_frame_is_last_);
}

void ReqRespBaseStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    LOG_DEBUG("HandleFrame: frame type: %d", frame->GetType());
    switch (frame->GetType()) {
        case FrameType::kHeaders:
            HandleHeaders(frame);
            break;

        case FrameType::kData:
            HandleData(frame);
            break;

        default:
            LOG_ERROR("ReqRespBaseStream::HandleFrame error");
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kFrameUnexpected);
            }
            break;
    }
}

bool ReqRespBaseStream::SendBodyWithProvider(const body_provider& provider) {
    is_provider_mode_ = true;
    provider_ = provider;
    HandleSent(0, 0);
    return true;
}

bool ReqRespBaseStream::SendBodyDirectly(const std::shared_ptr<common::IBuffer>& body) {
    if (!body || body->GetDataLength() == 0) {
        stream_->Close();
        LOG_DEBUG("SendBodyDirectly: empty body, closing stream with FIN");
        return true;
    }

    size_t total_sent = 0;

    LOG_DEBUG("SendBodyDirectly: body size: %zu", body->GetDataLength());

    while (body->GetDataLength() > 0) {
        // Get the actual remaining data length (this decreases as we consume data)
        size_t remaining = body->GetDataLength();
        size_t chunk_size = std::min<size_t>(kMaxDataFramePayload, remaining);

        // Use CloneReadable to extract chunk_size bytes and advance body's read
        // pointer
        auto chunk_buffer = body->CloneReadable(chunk_size);
        if (!chunk_buffer) {
            LOG_ERROR(
                "SendBodyDirectly CloneReadable failed. chunk "
                "size:%zu, total_sent:%zu, remaining:%zu",
                chunk_size, total_sent, remaining);
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            }
            return false;
        }

        DataFrame data_frame;
        data_frame.SetData(chunk_buffer);
        data_frame.SetLength(chunk_size);  // Explicitly set length

        auto data_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
        LOG_DEBUG(
            "SendBodyDirectly before encode frame. type:DataFrame, "
            "buffer_length:%u, chunk_size:%zu",
            data_buffer->GetDataLength(), chunk_size);
        if (!data_frame.Encode(data_buffer)) {
            LOG_ERROR(
                "SendBodyDirectly encode error. chunk size:%zu, "
                "total_sent:%zu, remaining:%zu",
                chunk_size, total_sent, remaining);
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            }
            return false;
        }
        LOG_DEBUG(
            "SendBodyDirectly after encode frame. type:DataFrame, buffer_length:%u", data_buffer->GetDataLength());

        if (!stream_->Flush()) {
            LOG_ERROR(
                "SendBodyDirectly send error. chunk size:%zu, "
                "total_sent:%zu, remaining:%zu",
                chunk_size, total_sent, remaining);
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            }
            return false;
        }

        // Log http3:frame_created event for each DATA frame
        {
            common::Http3FrameCreatedData created_data;
            created_data.frame_type = static_cast<uint16_t>(FrameType::kData);
            created_data.stream_id = GetStreamID();
            created_data.length = chunk_size;
            QLOG_HTTP3_FRAME_CREATED(qlog_trace_, created_data);
        }

        total_sent += chunk_size;
    }

    stream_->Close();
    LOG_DEBUG("SendBodyDirectly: sent %zu bytes, stream send direction closed (FIN)", total_sent);

    // Signal stream completion to the connection ONLY for response streams
    // Request streams need to wait for the response before being removed
    if (ShouldSignalCompletionAfterSend() && error_handler_) {
        error_handler_(GetStreamID(), 0);
    }
    return true;
}

bool ReqRespBaseStream::SendHeaders(const std::unordered_map<std::string, std::string>& headers) {
    auto headers_buffer =
        std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!qpack_encoder_->Encode(headers, headers_buffer)) {
        LOG_ERROR("SendHeaders error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        return false;
    }

    // Send HEADERS frame
    HeadersFrame headers_frame;
    headers_frame.SetEncodedFields(headers_buffer);
    auto frame_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    LOG_DEBUG("SendHeaders before encode frame. type:HeadersFrame, length:%u", frame_buffer->GetDataLength());
    if (!headers_frame.Encode(frame_buffer)) {
        LOG_ERROR("SendHeaders headers frame encode error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        return false;
    }
    LOG_DEBUG("SendHeaders after encode frame. type:HeadersFrame, length:%u", frame_buffer->GetDataLength());
    if (!stream_->Flush()) {
        LOG_ERROR("SendHeaders send headers error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
        }
        return false;
    }

    // Log http3:frame_created event
    {
        common::Http3FrameCreatedData created_data;
        created_data.frame_type = static_cast<uint16_t>(FrameType::kHeaders);
        created_data.stream_id = GetStreamID();
        created_data.length = frame_buffer->GetDataLength();
        QLOG_HTTP3_FRAME_CREATED(qlog_trace_, created_data);
    }

    return true;
}

void ReqRespBaseStream::HandleSent(uint32_t length, uint32_t error) {
    if (error != 0) {
        // H3_NO_ERROR (0x100 = 256) indicates graceful stream closure, not a real error
        if (error == static_cast<uint32_t>(Http3ErrorCode::kNoError)) {
            LOG_DEBUG("ReqRespBaseStream::HandleSent: stream closed gracefully (H3_NO_ERROR)");
            // For request streams (client side), H3_NO_ERROR from STOP_SENDING means
            // the server received our request and doesn't need more data. This is normal.
            // We should NOT trigger error_handler_ here because:
            // 1. The receive direction is still active (waiting for response data)
            // 2. Calling error_handler_ would trigger ScheduleStreamRemoval, destroying
            //    the stream while response data is still being received.
            // The stream will be properly cleaned up when the response is fully received
            // (HandleData with is_last=true, or HandleFinWithoutData).
            if (!ShouldSignalCompletionAfterSend()) {
                // Request stream: ignore H3_NO_ERROR from STOP_SENDING
                return;
            }
            // Response stream: signal completion (server sent response, got STOP_SENDING back)
            if (error_handler_) {
                error_handler_(GetStreamID(), error);
            }
            return;
        } else {
            LOG_ERROR("ReqRespBaseStream::HandleSent error: %d", error);
        }
        if (error_handler_) {
            error_handler_(GetStreamID(), error);
        }
        return;
    }
    LOG_DEBUG("ReqRespBaseStream::HandleSent: sent %u bytes", length);

    // If not in provider mode and all data sent, return early
    if (!is_provider_mode_) {
        return;
    }

    // If in provider mode and all data already sent, return early
    // This prevents calling provider again after it has returned 0
    if (all_provider_data_sent_) {
        return;
    }

    // ---------------------------------------------------------------------
    // Streaming-body backpressure.
    //
    // Without this guard the provider loop below will be called every time
    // a STREAM frame leaves the wire (sended_cb_ → HandleSent), and each
    // call eagerly pulls up to 64KB more bytes from the application body
    // provider into the QUIC send buffer. Because SendStream::Flush() does
    // not bound the buffer, the application ends up funneling its entire
    // payload (e.g. a 500 MB upload) into the QUIC send buffer in seconds,
    // long before the bytes are actually transmitted. That breaks two
    // things:
    //   (1) memory: O(file_size) instead of O(cwnd) buffered in-process;
    //   (2) progress reporting: bytes pulled from the provider no longer
    //       correspond to bytes the network has accepted, so a caller that
    //       updates a progress bar from inside the provider sees 100%
    //       almost immediately while the wire-level transfer is still in
    //       progress.
    //
    // The fix is application-level backpressure: do not call the provider
    // again while the per-stream send buffer is already deeper than a
    // small threshold (a few times the typical cwnd). When more bytes
    // drain to the wire, sended_cb_ will fire HandleSent() again and the
    // loop resumes naturally — no extra event/wakeup is needed.
    //
    // The threshold should be large enough to avoid stalling the network
    // (must comfortably exceed BDP for the path) but small enough that
    // the application's "bytes handed to QUIC" counter tracks "bytes on
    // the wire" within a window of order kBackpressureHighWatermark.
    // 256 KB is ~2× a typical loopback cwnd in our perf runs while still
    // being negligible against multi-MB / multi-GB payloads.
    constexpr uint64_t kBackpressureHighWatermark = 16 * 1024 * 1024; // EXPERIMENT: was 256KB
    if (stream_) {
        uint64_t pending = stream_->GetPendingSendBytes();
        if (pending >= kBackpressureHighWatermark) {
            LOG_DEBUG(
                "SendBodyWithProvider: backpressure, pending=%llu >= hwm=%llu, defer provider pull",
                static_cast<unsigned long long>(pending),
                static_cast<unsigned long long>(kBackpressureHighWatermark));
            return;
        }
    }

    // Optimized batching: Read multiple chunks from provider and send as single DataFrame
    // This reduces HTTP/3 frame overhead and improves throughput significantly
    const size_t kMaxBatchSize = 64 * 1024;  // 64KB per batch - matches upload optimization

    // Create a single buffer for the entire batch
    auto batch_buffer =
        std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());

    size_t total_bytes_this_batch = 0;

    // Keep reading from provider until we have 64KB or provider is done
    while (total_bytes_this_batch < kMaxBatchSize) {
        try {
            // Get whatever space is available in the current buffer chunk
            // MultiBlockBuffer will automatically allocate new chunks as needed
            auto span = batch_buffer->GetWritableSpan();
            if (!span.Valid()) {
                LOG_ERROR("SendBodyWithProvider: failed to get writable span (buffer allocation failed)");
                if (error_handler_) {
                    error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
                }
                return;
            }

            // Request only as much as we can write to current span
            size_t request_size =
                std::min(span.GetLength(), static_cast<uint32_t>(kMaxBatchSize - total_bytes_this_batch));
            size_t bytes_provided = provider_(span.GetStart(), request_size);
            LOG_DEBUG(
                "SendBodyWithProvider: bytes provided: %zu (requested %zu)", bytes_provided, request_size);

            if (bytes_provided > request_size) {
                LOG_ERROR("body provider returned invalid size %zu > %zu", bytes_provided, request_size);
                if (error_handler_) {
                    error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
                }
                return;
            }

            // Provider returned 0 - all data has been provided
            if (bytes_provided == 0) {
                // If we have accumulated data, send it first before closing
                if (total_bytes_this_batch > 0) {
                    break;  // Exit loop to send accumulated data
                }

                // No data accumulated and provider done - close stream
                stream_->Close();
                all_provider_data_sent_ = true;
                LOG_DEBUG(
                    "SendBodyWithProvider: provider returned 0, stream send "
                    "direction closed (FIN), total_bytes_this_batch=%zu",
                    total_bytes_this_batch);

                // Signal stream completion to the connection ONLY for response streams
                // Request streams need to wait for the response before being removed
                if (ShouldSignalCompletionAfterSend() && error_handler_) {
                    error_handler_(GetStreamID(), 0);
                }
                return;
            }

            batch_buffer->MoveWritePt(bytes_provided);
            total_bytes_this_batch += bytes_provided;

        } catch (const std::exception& e) {
            LOG_ERROR("body provider exception: %s", e.what());
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            }
            return;

        } catch (...) {
            LOG_ERROR("body provider unknown exception");
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            }
            return;
        }
    }

    // Send accumulated data as a single DataFrame
    if (total_bytes_this_batch > 0) {
        DataFrame data_frame;
        data_frame.SetData(batch_buffer);

        LOG_DEBUG("SendBodyWithProvider: sending batch DataFrame, size=%zu", batch_buffer->GetDataLength());

        auto frame_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
        if (!data_frame.Encode(frame_buffer)) {
            LOG_ERROR("encode error");
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            }
            return;
        }

        if (!stream_->Flush()) {
            LOG_ERROR("send error after batching %zu bytes", total_bytes_this_batch);
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            }
            return;
        }
        LOG_DEBUG("SendBodyWithProvider: flushed batch of %zu bytes", total_bytes_this_batch);

        // Log http3:frame_created event for batch DATA frame
        {
            common::Http3FrameCreatedData created_data;
            created_data.frame_type = static_cast<uint16_t>(FrameType::kData);
            created_data.stream_id = GetStreamID();
            created_data.length = total_bytes_this_batch;
            QLOG_HTTP3_FRAME_CREATED(qlog_trace_, created_data);
        }
    }
}

}  // namespace http3
}  // namespace quicx