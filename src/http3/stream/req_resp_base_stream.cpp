#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"

#include "quic/quicx/global_resource.h"

#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
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
            // FIN with no data: need to notify HTTP layer that stream ended
            // Call HandleData with empty buffer to trigger final callbacks
            common::LOG_DEBUG("ReqRespBaseStream::OnData: FIN with empty buffer, notifying end");

            // Create empty buffer and call HandleData
            static const auto empty_buffer = std::make_shared<common::SingleBlockBuffer>();
            HandleData(empty_buffer, true);
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
        for (size_t i = 0; i < frames.size(); i++) {
            // Only mark the last frame as is_last if we received FIN
            current_frame_is_last_ = is_last_data_ && (i == frames.size() - 1);
            HandleFrame(frames[i]);
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
                // emit Section Ack
                qpack_encoder_->EmitDecoderFeedback(0x00, header_block_key_);

                HandleHeaders();
            }
        });
        common::LOG_DEBUG("blocked header block key: %llu", header_block_key_);
        return;
    }
    // emit Section Ack
    qpack_encoder_->EmitDecoderFeedback(0x00, header_block_key_);

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
    const size_t kMaxDataFramePayload = 1400;  // TODO configurable
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

        total_sent += chunk_size;
    }

    stream_->Close();
    common::LOG_DEBUG("SendBodyDirectly: sent %zu bytes, stream send direction closed (FIN)", total_sent);
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
    return true;
}

void ReqRespBaseStream::HandleSent(uint32_t length, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("ReqRespBaseStream::HandleSent error: %d", error);
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

    auto buffer =
        std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    size_t total_sent = 0;
    size_t chunk_count = 0;
    const size_t kChunkSize = 1200;  // TODO configurable

    common::LOG_DEBUG("SendBodyWithProvider: body size: %zu", buffer->GetDataLength());

    // Pull data from provider
    size_t bytes_provided = 0;

    try {
        auto span = buffer->GetWritableSpan(kChunkSize);
        if (!span.Valid()) {
            common::LOG_ERROR("SendBodyWithProvider: failed to get writable span (buffer allocation failed)");
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            }
            return;
        }

        bytes_provided = provider_(span.GetStart(), span.GetLength());
        common::LOG_DEBUG("SendBodyWithProvider: bytes provided: %u", bytes_provided);
        buffer->MoveWritePt(bytes_provided);

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

    // Validate return value
    if (bytes_provided > kChunkSize) {
        common::LOG_ERROR("body provider returned invalid size %zu > %zu", bytes_provided, kChunkSize);
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        return;
    }

    // Create DATA frame
    DataFrame data_frame;
    data_frame.SetData(buffer);

    common::LOG_DEBUG(
        "SendBodyWithProvider before encode frame. type:DataFrame, "
        "buffer_length:%u",
        buffer->GetDataLength());

    // Encode and send
    auto frame_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!data_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("encode error at offset %zu", total_sent);
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        return;
    }

    if (bytes_provided > 0) {
        if (!stream_->Flush()) {
            common::LOG_ERROR("send error at offset %zu", total_sent);
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            }
            return;
        }
    }

    if (bytes_provided == 0) {
        // Close the send direction after sending all body data
        // This will send FIN to indicate end of data stream
        stream_->Close();
        all_provider_data_sent_ = true;
        common::LOG_DEBUG(
            "SendBodyWithProvider: sent %u bytes, stream send "
            "direction closed (FIN)",
            bytes_provided);
    }
}

}  // namespace http3
}  // namespace quicx