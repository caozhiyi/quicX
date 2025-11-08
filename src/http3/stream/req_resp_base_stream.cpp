#include "common/log/log.h"
#include "http3/http/error.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/headers_frame.h"
#include "http3/qpack/blocked_registry.h"
#include "common/buffer/buffer_read_view.h"
#include "http3/stream/req_resp_base_stream.h"

namespace quicx {
namespace http3 {

ReqRespBaseStream::ReqRespBaseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<quic::IQuicBidirectionStream>& stream,
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

void ReqRespBaseStream::OnData(std::shared_ptr<common::IBufferRead> data, bool is_last, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("ReqRespBaseStream::OnData error: %d", error);
        error_handler_(GetStreamID(), error);
        return;
    }

    is_last_data_ = is_last;
    
    // If buffer is empty (e.g., FIN without data, or RESET_STREAM), handle accordingly
    if (data->GetDataLength() == 0) {
        if (is_last) {
            // FIN received with no data: call HandleReadDone to finalize
            common::LOG_DEBUG("ReqRespBaseStream::OnData: FIN received with empty buffer");
            HandleData(std::vector<uint8_t>(), is_last);
        }
        // Empty buffer with no FIN: nothing to do (shouldn't happen normally)
        return;
    }
    
    std::vector<std::shared_ptr<IFrame>> frames;
    if (!DecodeFrames(data, frames)) {
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
    std::vector<uint8_t> encoded_fields = headers_frame->GetEncodedFields();
    auto headers_buffer = (std::make_shared<common::BufferReadView>(encoded_fields.data(), encoded_fields.size()));
    if (!qpack_encoder_->Decode(headers_buffer, headers_)) {
        // If blocked (RIC not satisfied), enqueue a retry once insert count increases
        blocked_registry_->Add(header_block_key_, [this, encoded_fields]() {
            auto view = std::make_shared<common::BufferReadView>(const_cast<uint8_t*>(encoded_fields.data()), static_cast<uint32_t>(encoded_fields.size()));
            std::unordered_map<std::string, std::string> tmp;
            if (qpack_encoder_->Decode(view, tmp)) {
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
    const size_t kChunkSize = 2048; // 2KB chunks
    uint8_t chunk_buffer[kChunkSize];
    size_t total_sent = 0;
    size_t chunk_count = 0;
    
    while (true) {
        // Pull data from provider
        size_t bytes_provided = 0;
        
        try {
            bytes_provided = provider(chunk_buffer, kChunkSize);

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
        std::vector<uint8_t> data(chunk_buffer, chunk_buffer + bytes_provided);
        data_frame.SetData(data);
        
        // Encode and send
        uint8_t frame_buf[4096]; // Extra space for frame header
        auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
        
        if (!data_frame.Encode(frame_buffer)) {
            common::LOG_ERROR("encode error at offset %zu", total_sent);
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;
        }
        
        int32_t sent = stream_->Send(frame_buffer);
        if (sent <= 0) {
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

bool ReqRespBaseStream::SendBodyDirectly(const std::string& body) {
    const size_t kMaxDataFramePayload = 2048; // 2KB per DATA frame TODO configurable
    size_t total_sent = 0;
        
     while (total_sent < body.size()) {
        size_t chunk_size = std::min(kMaxDataFramePayload, body.size() - total_sent);
            
        // TODO do not copy body
        DataFrame data_frame;
        std::vector<uint8_t> chunk(body.begin() + total_sent, body.begin() + total_sent + chunk_size);
        data_frame.SetData(chunk);
            
        uint8_t data_buf[4096]; // Buffer should be larger than payload to accommodate frame header， TODO configurable
        auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
        if (!data_frame.Encode(data_buffer)) {
            common::LOG_ERROR("RequestStream::SendRequest data frame encode error. chunk size:%zu, offset:%zu, total:%zu", 
                            chunk_size, total_sent, body.size());
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;
        }
        
        if (stream_->Send(data_buffer) <= 0) {
            common::LOG_ERROR("RequestStream::SendRequest send data error. chunk size:%zu, offset:%zu, total:%zu", 
                                chunk_size, total_sent, body.size());
            error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            return false;
        }
            
        total_sent += chunk_size;
    }
    // Close the send direction after sending all body data
    // This will send FIN to indicate end of data stream
    stream_->Close();
    common::LOG_DEBUG("SendBodyDirectly: sent %zu bytes, stream send direction closed (FIN)", total_sent);
    return true;
}

bool ReqRespBaseStream::SendHeaders(const std::unordered_map<std::string, std::string>& headers) {
    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(headers, headers_buffer)) {
        common::LOG_ERROR("SendHeaders error");
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return false;
    }
    
    // Send HEADERS frame
    HeadersFrame headers_frame;
    std::vector<uint8_t> encoded_fields(headers_buffer->GetData(), headers_buffer->GetData() + headers_buffer->GetDataLength());
    headers_frame.SetEncodedFields(encoded_fields);

    uint8_t frame_buf[4096]; // TODO: Use dynamic buffer
    auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
    if (!headers_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("SendHeaders headers frame encode error");
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return false;
    }
    if (stream_->Send(frame_buffer) <= 0) {
        common::LOG_ERROR("SendHeaders send headers error");
        error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
        return false;
    }
    return true;
}

}
}