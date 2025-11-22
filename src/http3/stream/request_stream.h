#ifndef HTTP3_STREAM_REQUEST_STREAM
#define HTTP3_STREAM_REQUEST_STREAM

#include <memory>
#include <unordered_map>

#include "http3/include/type.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/include/if_async_handler.h"
#include "http3/stream/req_resp_base_stream.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief RequestStream is the stream for sending requests to the server
 *
 * The request stream is used to send requests to the server.
 * It is responsible for sending the request headers and data to the server.
 */
class RequestStream: public ReqRespBaseStream {
public:
    RequestStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
        const std::shared_ptr<IQuicBidirectionStream>& stream, std::shared_ptr<IAsyncClientHandler> async_handler,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const std::function<void(std::unordered_map<std::string, std::string>&, uint64_t push_id)>&
            push_promise_handler);

    RequestStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
        const std::shared_ptr<IQuicBidirectionStream>& stream, http_response_handler response_handler,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const std::function<void(std::unordered_map<std::string, std::string>&, uint64_t push_id)>&
            push_promise_handler);

    virtual ~RequestStream();

    // Send request/response
    bool SendRequest(std::shared_ptr<IRequest> request);

private:
    virtual void HandleHeaders() override;
    virtual void HandleData(const std::shared_ptr<common::IBuffer>& data, bool is_last) override;
    virtual void HandleFrame(std::shared_ptr<IFrame> frame) override;
    void HandlePushPromise(std::shared_ptr<IFrame> frame);

private:
    uint32_t body_length_;
    uint32_t received_body_length_;

    std::shared_ptr<IResponse> response_;
    http_response_handler response_handler_;
    std::shared_ptr<IAsyncClientHandler> async_handler_;
    std::function<void(std::unordered_map<std::string, std::string>&, uint64_t)> push_promise_handler_;
};

}  // namespace http3
}  // namespace quicx

#endif
