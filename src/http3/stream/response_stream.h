#ifndef HTTP3_STREAM_RESPONSE_STREAM
#define HTTP3_STREAM_RESPONSE_STREAM

#include <memory>
#include <string>
#include <unordered_map>

#include "http3/http/request.h"
#include "http3/http/response.h"
#include "http3/include/type.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/router/if_router.h"
#include "http3/stream/req_resp_base_stream.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief HttpProcessor is the base class for all http processors
 *
 * The http processor is used to process the http request and response.
 * It is responsible for matching the route, before and after the handler process.
 */
class IHttpProcessor {
public:
    virtual RouteConfig MatchRoute(
        HttpMethod method, const std::string& path, std::shared_ptr<IRequest> request = nullptr) = 0;
    virtual void BeforeHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) = 0;
    virtual void AfterHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) = 0;
};

/**
 * @brief ResponseStream is the stream for sending responses to the client
 *
 * The response stream is used to send responses to the client.
 * It is responsible for sending the response headers and data to the client.
 */
class ResponseStream: public ReqRespBaseStream, public std::enable_shared_from_this<ResponseStream> {
public:
    ResponseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
        const std::shared_ptr<IQuicBidirectionStream>& stream, std::shared_ptr<IHttpProcessor> http_processor,
        const std::function<void(std::shared_ptr<IResponse>, std::shared_ptr<ResponseStream>)> push_handler,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const std::function<bool()>& settings_received_cb);
    virtual ~ResponseStream();

    void SendPushPromise(const std::unordered_map<std::string, std::string>& headers, int32_t push_id);
    bool SendResponse(std::shared_ptr<IResponse> response);

private:
    virtual void HandleHeaders() override;
    virtual void HandleData(const std::shared_ptr<common::IBuffer>& data, bool is_last) override;
    // call upper layer handler
    void HandleHttp(
        std::function<void(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response)> handler);
    // send response and handle push
    void HandleResponse();

private:
    uint32_t body_length_;
    uint32_t received_body_length_;
    bool is_response_sent_;
    RouteConfig route_config_;
    std::shared_ptr<Request> request_;    // Request object (created in HandleHeaders)
    std::shared_ptr<Response> response_;  // Response object (created in HandleHeaders, used in HandleData)
    std::shared_ptr<IHttpProcessor> http_processor_;
    std::function<void(std::shared_ptr<IResponse>, std::shared_ptr<ResponseStream>)> push_handler_;
    std::function<bool()> settings_received_cb_;  // RFC 9114: Check if peer SETTINGS received
};

}  // namespace http3
}  // namespace quicx

#endif
