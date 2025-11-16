#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/headers_frame.h"
#include "quic/quicx/global_resource.h"
#include "http3/qpack/blocked_registry.h"
#include "common/buffer/multi_block_buffer.h"
#include "http3/stream/req_resp_base_stream.h"

namespace quicx {
namespace http3 {

ReqRespBaseStream::ReqRespBaseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<IQuicBidirectionStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IStream(StreamType::kReqResp, error_handler),
    is_last_data_(false),
    qpack_encoder_(qpack_encoder),
    blocked_registry_(blocked_registry),
    stream_(stream) {

    stream_->SetStreamReadCallBack(std::bind(&ReqRespBaseStream::OnData,
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

ReqRespBaseStream::~ReqRespBaseStream() {
    // Do NOT call Close() here - it should have been called after sending the last frame
    // Calling Close() in destructor may cause issues if the stream is already closed
    // or if the stream state is not suitable for closing
}

void ReqRespBaseStream::OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("ReqRespBaseStream::OnData error: %d", error);
        error_handler_(GetStreamID(), error);
        return;
    }

    is_last_data_ = is_last;

    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(data);
    // If buffer is empty (e.g., FIN without data, or RESET_STREAM), handle accordingly
    if (data->GetDataLength() == 0) {
        if (is_last) {
            // FIN received with no data: call HandleReadDone to finalize
            common::LOG_DEBUG("ReqRespBaseStream::OnData: FIN received with empty buffer");
            HandleData(nullptr);
        }
        // Empty buffer with no FIN: nothing to do (shouldn't happen normally)
        return;
    }
    
    std::vector<std::shared_ptr<IFrame>> frames;
    if (!DecodeFrames(buffer, frames)) {
        common::LOG_ERROR("ReqRespBaseStream::OnData decode frames error");
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return;

    } else {
        for (const auto& frame : frames) {
            HandleFrame(frame);
        }
    }
}

void ReqRespBaseStream::HandleHeaders(std::shared_ptr<IFrame> frame) {
    auto headers_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    if (!headers_frame) {   
        common::LOG_ERROR("ReqRespBaseStream::HandleHeaders error");
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
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
        // If blocked (RIC not satisfied), enqueue a retry once insert count increases
        blocked_registry_->Add(header_block_key_, [this, encoded_fields]() {
            std::unordered_map<std::string, std::string> tmp;
            if (qpack_encoder_->Decode(encoded_fields, tmp)) {
                // emit Section Ack
                qpack_encoder_->EmitDecoderFeedback(0x00, header_block_key_);

                HandleHeaders();
            }
        });
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
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return;
    }

    const auto& data = data_frame->GetData();
    
    HandleData(data, is_last_data_);
}

void ReqRespBaseStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    switch (frame->GetType()) {
    case FrameType::kHeaders:
        HandleHeaders(frame);
        break;

    case FrameType::kData:
        HandleData(frame);
        break;

    default:
        common::LOG_ERROR("ReqRespBaseStream::HandleFrame error");
        error_handler_(GetStreamID(), Http3ErrorCode::kFrameUnexpected);
        break;
    }
}

bool ReqRespBaseStream::SendBodyWithProvider(const body_provider& provider) {
    auto buffer = std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    size_t total_sent = 0;
    size_t chunk_count = 0;
    const size_t kChunkSize = 2048; // TODO configurable
    
    while (true) {
        // Pull data from provider
        size_t bytes_provided = 0;
        
        try {
            auto span = buffer->GetWritableSpan(kChunkSize);
            bytes_provided = provider(span.GetStart(), span.GetLength());
            buffer->MoveWritePt(bytes_provided);

        } catch (const std::exception& e) {
            common::LOG_ERROR("body provider exception: %s", e.what());
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;

        } catch (...) {
            common::LOG_ERROR("body provider unknown exception");
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;
        }

        // Validate return value
        if (bytes_provided > kChunkSize) {
            common::LOG_ERROR("body provider returned invalid size %zu > %zu", 
                            bytes_provided, kChunkSize);
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;
        }

        if (bytes_provided == 0) {
            // End of body
            common::LOG_DEBUG("body complete, sent %zu bytes in %zu chunks", 
                            total_sent, chunk_count);
            break;
        }
        
        // Create DATA frame
        DataFrame data_frame;
        data_frame.SetData(buffer);
        
        // Encode and send
        auto frame_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
        if (!data_frame.Encode(frame_buffer)) {
            common::LOG_ERROR("encode error at offset %zu", total_sent);
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;
        }
        
        if (!stream_->Flush()) {
            common::LOG_ERROR("send error at offset %zu", total_sent);
            error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            return false;
        }
        
        total_sent += bytes_provided;
        chunk_count++;
        
        // Log progress for large transfers
        if (total_sent % (1024 * 1024) == 0) {
            common::LOG_INFO("sent %zu MB", total_sent / 1024 / 1024);
        }
    }
    // Close the send direction after sending all body data
    // This will send FIN to indicate end of data stream
    stream_->Close();
    common::LOG_DEBUG("SendBodyWithProvider: sent %zu bytes in %zu chunks, stream send direction closed (FIN)", total_sent, chunk_count);
    return true;
}

bool ReqRespBaseStream::SendBodyDirectly(const std::shared_ptr<common::IBuffer>& body) {
    const size_t kMaxDataFramePayload = 2048; // TODO configurable
    if (!body || body->GetDataLength() == 0) {
        stream_->Close();
        common::LOG_DEBUG("SendBodyDirectly: empty body, closing stream with FIN");
        return true;
    }

    size_t total_sent = 0;
    size_t offset = 0;
        
    while (body->GetDataLength() > 0) {
        size_t chunk_size = std::min<size_t>(kMaxDataFramePayload, body->GetDataLength() - offset);

        DataFrame data_frame;
        data_frame.SetData(body);
        data_frame.SetLength(chunk_size);

        auto data_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
        if (!data_frame.Encode(data_buffer)) {
            common::LOG_ERROR("SendBodyDirectly encode error. chunk size:%zu, offset:%zu, total:%zu",
                              chunk_size, total_sent, body->GetDataLength());
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;
        }

        if (!stream_->Flush()) {
            common::LOG_ERROR("SendBodyDirectly send error. chunk size:%zu, offset:%zu, total:%zu",
                              chunk_size, total_sent, body->GetDataLength());
            error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            return false;
        }

        total_sent += chunk_size;
        offset += chunk_size;
    }

    stream_->Close();
    common::LOG_DEBUG("SendBodyDirectly: sent %zu bytes, stream send direction closed (FIN)", total_sent);
    return true;
}

bool ReqRespBaseStream::SendHeaders(const std::unordered_map<std::string, std::string>& headers) {
    auto headers_buffer = std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!qpack_encoder_->Encode(headers, headers_buffer)) {
        common::LOG_ERROR("SendHeaders error");
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return false;
    }
    
    // Send HEADERS frame
    HeadersFrame headers_frame;
    headers_frame.SetEncodedFields(headers_buffer);

    auto frame_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!headers_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("SendHeaders headers frame encode error");
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return false;
    }
    if (!stream_->Flush()) {
        common::LOG_ERROR("SendHeaders send headers error");
        error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
        return false;
    }
    return true;
}

}
}