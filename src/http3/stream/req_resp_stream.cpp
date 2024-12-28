// #include "common/buffer/buffer.h"
// #include "http3/frame/data_frame.h"
// #include "http3/frame/headers_frame.h"
// #include "http3/stream/req_resp_stream.h"

// namespace quicx {
// namespace http3 {

// ReqRespStream::ReqRespStream(std::shared_ptr<QpackEncoder> qpack_encoder,
//     std::shared_ptr<quic::IQuicBidirectionStream> stream) :
//     qpack_encoder_(qpack_encoder),
//     stream_(stream) {
// }

// ReqRespStream::~ReqRespStream() {
//     if (stream_) {
//         stream_->Close();
//     }
// }

// bool ReqRespStream::SendRequest(std::shared_ptr<IRequest> request) {
//     // Send HEADERS frame
//     HeadersFrame headers_frame;
//     headers_frame.SetHeaders(request->GetHeaders());

//     uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
//     auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
//     if (!headers_frame.Encode(headers_buffer)) {
//         return false;
//     }
//     if (stream_->Send(headers_buffer->GetData(), headers_buffer->GetDataLength()) <= 0) {
//         return false;
//     }

//     // Send DATA frame if body exists
//     if (request->GetBody().length() > 0) {
//         DataFrame data_frame;
//         data_frame.SetData((uint8_t*)request->GetBody().data(), request->GetBody().length());

//         uint8_t data_buf[4096]; // TODO: Use dynamic buffer
//         auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
//         if (!data_frame.Encode(data_buffer)) {
//             return false;
//         }
//         if (stream_->Send(data_buffer->GetData(), data_buffer->GetDataLength()) <= 0) {
//             return false;
//         }
//     }

//     return true;
// }

// bool ReqRespStream::SendResponse(std::shared_ptr<IResponse> response) {
//     // Send HEADERS frame
//     HeadersFrame headers_frame;
//     headers_frame.SetHeaders(response->GetHeaders());

//     uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
//     auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
//     if (!headers_frame.Encode(headers_buffer)) {
//         return false;
//     }
//     if (stream_->Send(headers_buffer->GetData(), headers_buffer->GetDataLength()) <= 0) {
//         return false;
//     }

//     // Send DATA frame if body exists
//     if (response->GetBody().length() > 0) {
//         DataFrame data_frame;
//         data_frame.SetData((uint8_t*)response->GetBody().data(), response->GetBody().length());

//         uint8_t data_buf[4096]; // TODO: Use dynamic buffer
//         auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
//         if (!data_frame.Encode(data_buffer)) {
//             return false;
//         }
//         if (stream_->Send(data_buffer->GetData(), data_buffer->GetDataLength()) <= 0) {
//             return false;
//         }
//     }

//     return true;
// }

// }
// }
