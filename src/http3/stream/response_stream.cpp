#include "common/log/log.h"
#include "http3/http/request.h"
#include "http3/http/response.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/response_stream.h"

namespace quicx {
namespace http3 {

ResponseStream::ResponseStream(std::shared_ptr<QpackEncoder> qpack_encoder,
    std::shared_ptr<quic::IQuicBidirectionStream> stream,
    http_handler handler) :
    qpack_encoder_(qpack_encoder),
    stream_(stream),
    handler_(handler) {

    // Set callback for receiving request data
    stream_->SetStreamReadCallBack(std::bind(&ResponseStream::OnRequest, 
        this, std::placeholders::_1, std::placeholders::_2));
}

ResponseStream::~ResponseStream() {
    if (stream_) {
        stream_->Close();
    }
}

void ResponseStream::OnRequest(std::shared_ptr<common::IBufferRead> data, uint32_t error) {
    // decode request
    Request request;
    if (error != 0) {
        common::LOG_ERROR("ResponseStream::OnRequest error: %d", error);
        return;
    }

    // decode response headers
    HeadersFrame request_headers_frame;
    if (!request_headers_frame.Decode(data)) {
        common::LOG_ERROR("ResponseStream::OnRequest error");
        return;
    }

    // decode response body
    DataFrame request_data_frame;
    if (!request_data_frame.Decode(data)) {
        common::LOG_ERROR("ResponseStream::OnRequest error");
        return;
    }

    // qpack decode headers
    std::unordered_map<std::string, std::string> headers;
    if (!qpack_encoder_->Decode(request_headers_frame.GetEncodedFields(), headers)) {
        common::LOG_ERROR("ResponseStream::OnRequest error");
        return;
    }
    request.SetHeaders(headers);
    request.SetBody(std::string(request_data_frame.GetData().begin(), request_data_frame.GetData().end()));

    // handle request
    Response response;
    handler_(request, response);

    // Decode request headers
    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(response.GetHeaders(), headers_buffer)) {
        common::LOG_ERROR("ResponseStream::OnRequest error");
        return;
    }
    
    // Send HEADERS frame
    HeadersFrame response_headers_frame;
    response_headers_frame.SetEncodedFields(headers_buffer);

    uint8_t frame_buf[4096]; // TODO: Use dynamic buffer
    auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
    if (!response_headers_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("ResponseStream::OnRequest error");
        return;
    }
    if (stream_->Send(frame_buffer) <= 0) {
        common::LOG_ERROR("ResponseStream::OnRequest error");
        return;
    }

    // Send DATA frame if body exists
    if (response.GetBody().length() > 0) {
        DataFrame data_frame; // TODO: may send more than one DATA frame
        std::vector<uint8_t> body(request.GetBody().begin(), request.GetBody().end());
        data_frame.SetData(body);           

        uint8_t data_buf[4096]; // TODO: Use dynamic buffer
        auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
        if (!data_frame.Encode(data_buffer)) {
            common::LOG_ERROR("ResponseStream::OnRequest error");
            return;
        }
        if (stream_->Send(data_buffer) <= 0) {
            common::LOG_ERROR("ResponseStream::OnRequest error");
            return;
        }
    }
}

}
}
