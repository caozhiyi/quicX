#include "http3/http/response.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/request_stream.h"


namespace quicx {
namespace http3 {

RequestStream::RequestStream(std::shared_ptr<QpackEncoder> qpack_encoder,
    std::shared_ptr<quic::IQuicBidirectionStream> stream) :
    qpack_encoder_(qpack_encoder),
    stream_(stream) {

    // Set callback for receiving response data
    stream_->SetStreamReadCallBack(std::bind(&RequestStream::OnResponse, 
        this, std::placeholders::_1, std::placeholders::_2));
}

RequestStream::~RequestStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool RequestStream::SendRequest(const IRequest& request, const http_response_handler& handler) {
    handler_ = handler;

    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(request.GetHeaders(), headers_buffer)) {
        return false;
    }
    
    // Send HEADERS frame
    HeadersFrame headers_frame;
    headers_frame.SetEncodedFields(headers_buffer);

    uint8_t frame_buf[4096]; // TODO: Use dynamic buffer
    auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
    if (!headers_frame.Encode(frame_buffer)) {
        return false;
    }
    if (stream_->Send(frame_buffer) <= 0) {
        return false;
    }

    // Send DATA frame if body exists
    if (request.GetBody().length() > 0) {
        DataFrame data_frame; // TODO: may send more than one DATA frame
        std::vector<uint8_t> body(request.GetBody().begin(), request.GetBody().end());
        data_frame.SetData(body);           

        uint8_t data_buf[4096]; // TODO: Use dynamic buffer
        auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
        if (!data_frame.Encode(data_buffer)) {
            return false;
        }
        if (stream_->Send(data_buffer) <= 0) {
            return false;
        }
    }

    return true;
}

void RequestStream::OnResponse(std::shared_ptr<common::IBufferRead> data, uint32_t error) {
    Response response;
    if (error != 0) {
        handler_(response, error);
        return;
    }

    // decode response headers
    HeadersFrame headers_frame;
    if (!headers_frame.Decode(data)) {
        handler_(response, -1);
        return;
    }

    // decode response body
    DataFrame data_frame;
    if (!data_frame.Decode(data)) {
        handler_(response, -1);
        return;
    }

    // qpack decode headers
    std::unordered_map<std::string, std::string> headers;
    if (!qpack_encoder_->Decode(headers_frame.GetEncodedFields(), headers)) {
        handler_(response, -1);
        return;
    }
    response.SetStatusCode(std::stoi(headers[":status"]));
    response.SetHeaders(headers);
    response.SetBody(std::string(data_frame.GetData().begin(), data_frame.GetData().end()));

    // handle response
    handler_(response, 0);
}

}
}