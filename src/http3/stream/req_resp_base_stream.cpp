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
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<IQuicBidirectionStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IStream(StreamType::kReqResp, error_handler),
    is_last_data_(false),
    current_frame_is_last_(false),
    qpack_encoder_(qpack_encoder),
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
        common::LOG_ERROR("ReqRespBaseStream::OnData error: %d", error);
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
            // FIN with no data: notify subclass to handle stream completion
            common::LOG_DEBUG("ReqRespBaseStream::OnData: FIN with empty buffer, notifying end");
            HandleFinWithoutData();
        }
        // Check if we need to notify completion
        if (should_notify_completion_) {
            common::LOG_DEBUG("ReqRespBaseStream::OnData: notifying stream completion after empty data");
            if (error_handler_) {
                error_handler_(GetStreamID(), 0);
            }
            should_notify_completion_ = false;
        }
        return;
    }

    std::vector<std::shared_ptr<IFrame>> frames;
    if (!frame_decoder_.DecodeFrames(buffer, frames)) {
        common::LOG_ERROR("ReqRespBaseStream::OnData decode frames error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        }
        return;

    } else {
        // RFC 9114: Only the LAST frame in this batch should be marked is_last
        common::LOG_DEBUG("ReqRespBaseStream::OnData: processing %zu frames, is_last_data=%d", 
            frames.size(), is_last_data_);
        for (size_t i = 0; i < frames.size(); i++) {
            // Only mark the last frame as is_last if we received FIN
            current_frame_is_last_ = is_last_data_ && (i == frames.size() - 1);
            common::LOG_DEBUG("ReqRespBaseStream::OnData: processing frame %zu/%zu, type=0x%x, is_last=%d", 
                i+1, frames.size(), static_cast<uint32_t>(frames[i]->GetType()), current_frame_is_last_);
            HandleFrame(frames[i]);
        }

        // CRITICAL FIX: If we got FIN but no frames were decoded,
        // we need to notify via HandleFinWithoutData (implemented in subclass)
        if (is_last_data_ && frames.empty()) {
            common::LOG_DEBUG("ReqRespBaseStream::OnData: FIN received but no frames decoded");
            HandleFinWithoutData();
        }

        // Process all frames first (including PUSH_PROMISE), then notify stream completion
        // This ensures PUSH_PROMISE frames are not ignored due to early stream cleanup
        if (should_notify_completion_) {
            common::LOG_DEBUG("ReqRespBaseStream::OnData: notifying stream completion after processing all frames");
            if (error_handler_) {
                error_handler_(GetStreamID(), 0);
            }
            should_notify_completion_ = false;
        }
    }
}

void ReqRespBaseStream::HandleHeaders(std::shared_ptr<IFrame> frame) {
    auto headers_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    if (!headers_frame) {
        common::LOG_ERROR("ReqRespBaseStream::HandleHeaders error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        }
        return;
    }

    // TODO check if headers is complete and headers length is correct

    // Assign a real header-block-id = (stream_id << 32) | section_number
    if (header_block_key_ == 0) {
        uint64_t sid = GetStreamID();
        uint64_t secno = static_cast<uint64_t>(++next_section_number_);
        header_block_key_ = (sid << 32) | secno;
    }
    // Decode headers using QPACK
    auto encoded_fields = headers_frame->GetEncodedFields();
    if (!qpack_encoder_->Decode(encoded_fields, headers_)) {
        // If blocked (RIC not satisfied), enqueue a retry once insert count
        // increases
        blocked_registry_->Add(header_block_key_, [this, encoded_fields]() {
            std::unordered_map<std::string, std::string> tmp;
            if (qpack_encoder_->Decode(encoded_fields, tmp)) {
                // RFC 9204 §4.4.1: Only emit Section Ack when RIC > 0.
                if (qpack_encoder_->GetLastDecodedRequiredInsertCount() > 0) {
                    qpack_encoder_->EmitDecoderFeedback(0x00, header_block_key_);
                }

                HandleHeaders();
            }
        });
        common::LOG_DEBUG("blocked header block key: %llu", header_block_key_);
        return;
    }
    // RFC 9204 §4.4.1: The decoder MUST NOT emit Section Acknowledgment for
    // header blocks that were processed without a dependency on the dynamic
    // table (i.e., with a Required Insert Count of 0). Emitting one causes
    // the peer to raise QPACK_DECODER_STREAM_ERROR because its encoder does
    // not track streams that only use static-table references.
    if (qpack_encoder_->GetLastDecodedRequiredInsertCount() > 0) {
        qpack_encoder_->EmitDecoderFeedback(0x00, header_block_key_);
    }

    HandleHeaders();
}

void ReqRespBaseStream::HandleData(std::shared_ptr<IFrame> frame) {
    auto data_frame = std::dynamic_pointer_cast<DataFrame>(frame);
    if (!data_frame) {
        common::LOG_ERROR("ReqRespBaseStream::HandleData error");
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
    common::LOG_DEBUG("HandleFrame: frame type: %d", frame->GetType());
    switch (frame->GetType()) {
        case FrameType::kHeaders:
            HandleHeaders(frame);
            break;

        case FrameType::kData:
            HandleData(frame);
            break;

        default:
            common::LOG_ERROR("ReqRespBaseStream::HandleFrame error");
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
        common::LOG_DEBUG("SendBodyDirectly: empty body, closing stream with FIN");
        return true;
    }

    size_t total_sent = 0;

    common::LOG_DEBUG("SendBodyDirectly: body size: %zu", body->GetDataLength());

    while (body->GetDataLength() > 0) {
        // Get the actual remaining data length (this decreases as we consume data)
        size_t remaining = body->GetDataLength();
        size_t chunk_size = std::min<size_t>(kMaxDataFramePayload, remaining);

        // Use CloneReadable to extract chunk_size bytes and advance body's read
        // pointer
        auto chunk_buffer = body->CloneReadable(chunk_size);
        if (!chunk_buffer) {
            common::LOG_ERROR(
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
        common::LOG_DEBUG(
            "SendBodyDirectly before encode frame. type:DataFrame, "
            "buffer_length:%u, chunk_size:%zu",
            data_buffer->GetDataLength(), chunk_size);
        if (!data_frame.Encode(data_buffer)) {
            common::LOG_ERROR(
                "SendBodyDirectly encode error. chunk size:%zu, "
                "total_sent:%zu, remaining:%zu",
                chunk_size, total_sent, remaining);
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            }
            return false;
        }
        common::LOG_DEBUG(
            "SendBodyDirectly after encode frame. type:DataFrame, buffer_length:%u", data_buffer->GetDataLength());

        if (!stream_->Flush()) {
            common::LOG_ERROR(
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
    common::LOG_DEBUG("SendBodyDirectly: sent %zu bytes, stream send direction closed (FIN)", total_sent);

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
        common::LOG_ERROR("SendHeaders error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        return false;
    }

    // Send HEADERS frame
    HeadersFrame headers_frame;
    headers_frame.SetEncodedFields(headers_buffer);
    auto frame_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    common::LOG_DEBUG("SendHeaders before encode frame. type:HeadersFrame, length:%u", frame_buffer->GetDataLength());
    if (!headers_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("SendHeaders headers frame encode error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        return false;
    }
    common::LOG_DEBUG("SendHeaders after encode frame. type:HeadersFrame, length:%u", frame_buffer->GetDataLength());
    if (!stream_->Flush()) {
        common::LOG_ERROR("SendHeaders send headers error");
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
            common::LOG_DEBUG("ReqRespBaseStream::HandleSent: stream closed gracefully (H3_NO_ERROR)");
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
            common::LOG_ERROR("ReqRespBaseStream::HandleSent error: %d", error);
        }
        if (error_handler_) {
            error_handler_(GetStreamID(), error);
        }
        return;
    }
    common::LOG_DEBUG("ReqRespBaseStream::HandleSent: sent %u bytes", length);

    // If not in provider mode and all data sent, return early
    if (!is_provider_mode_) {
        return;
    }

    // If in provider mode and all data already sent, return early
    // This prevents calling provider again after it has returned 0
    if (all_provider_data_sent_) {
        return;
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
                common::LOG_ERROR("SendBodyWithProvider: failed to get writable span (buffer allocation failed)");
                if (error_handler_) {
                    error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
                }
                return;
            }

            // Request only as much as we can write to current span
            size_t request_size =
                std::min(span.GetLength(), static_cast<uint32_t>(kMaxBatchSize - total_bytes_this_batch));
            size_t bytes_provided = provider_(span.GetStart(), request_size);
            common::LOG_DEBUG(
                "SendBodyWithProvider: bytes provided: %zu (requested %zu)", bytes_provided, request_size);

            if (bytes_provided > request_size) {
                common::LOG_ERROR("body provider returned invalid size %zu > %zu", bytes_provided, request_size);
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
                common::LOG_DEBUG(
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
            common::LOG_ERROR("body provider exception: %s", e.what());
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            }
            return;

        } catch (...) {
            common::LOG_ERROR("body provider unknown exception");
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

        common::LOG_DEBUG("SendBodyWithProvider: sending batch DataFrame, size=%zu", batch_buffer->GetDataLength());

        auto frame_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
        if (!data_frame.Encode(frame_buffer)) {
            common::LOG_ERROR("encode error");
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            }
            return;
        }

        if (!stream_->Flush()) {
            common::LOG_ERROR("send error after batching %zu bytes", total_bytes_this_batch);
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            }
            return;
        }
        common::LOG_DEBUG("SendBodyWithProvider: flushed batch of %zu bytes", total_bytes_this_batch);

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