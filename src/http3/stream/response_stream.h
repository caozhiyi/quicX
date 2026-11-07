#ifndef HTTP3_STREAM_RESPONSE_STREAM
#define HTTP3_STREAM_RESPONSE_STREAM

#include <memory>
#include <quicx/http3/type.h>
#include <quicx/quic/if_quic_bidirection_stream.h>
#include <string>
#include <unordered_map>

#include "http3/http/request.h"
#include "http3/http/response.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/router/if_router.h"
#include "http3/stream/req_resp_base_stream.h"

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
    // P1-H8: before-middlewares can short-circuit the pipeline. Returns false
    // when a middleware did so — the route handler and after-middlewares must
    // be skipped and the middleware-produced response flushed by the stream.
    virtual bool BeforeHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) = 0;
    virtual void AfterHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) = 0;
    // P1-H6g: per-server-instance request body cap (bytes). ResponseStream
    // pulls it at construction; the previous process-wide static made two
    // Server objects with different configs silently share one limit.
    // Default matches Http3ServerConfig::max_request_body_size_.
    virtual uint64_t GetMaxRequestBodySize() const { return 16 * 1024 * 1024; }
};

/**
 * @brief ResponseStream is the stream for sending responses to the client
 *
 * The response stream is used to send responses to the client.
 * It is responsible for sending the response headers and data to the client.
 */
class ResponseStream: public ReqRespBaseStream {
public:
    ResponseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<QpackDecoder>& qpack_decoder,
        const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
        const std::shared_ptr<IQuicBidirectionStream>& stream, std::weak_ptr<IHttpProcessor> http_processor,
        const std::function<void(std::shared_ptr<IResponse>, std::shared_ptr<ResponseStream>)> push_handler,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const std::function<bool()>& settings_received_cb);
    virtual ~ResponseStream();

    // P1-H6f: RFC 9114 §4.6 — MAX_PUSH_ID is a full varint on the wire;
    // a 32-bit parameter truncates push ids at 2^31. Callers pass the
    // server-allocated push id as uint64_t.
    void SendPushPromise(const HttpFields& headers, uint64_t push_id);
    bool SendResponse(std::shared_ptr<IResponse> response);

    // ResponseStream should signal completion after sending response body
    // because the server's job is done after sending the response
    bool ShouldSignalCompletionAfterSend() const override { return true; }

private:
    virtual void HandleHeaders() override;
    virtual void HandleData(const std::shared_ptr<common::IBuffer>& data, bool is_last) override;
    virtual void HandleFinWithoutData() override;
    // call upper layer handler
    void HandleHttp(
        std::function<void(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response)> handler);
    // send response and handle push
    void HandleResponse();

    // Reject the request with 413 and terminate the stream. Used when the
    // declared or received body size exceeds max_request_body_size_.
    void SendPayloadTooLarge();

private:
    // P1-H6g: per-instance upper bound (bytes) for request bodies buffered in
    // complete mode; defends against unbounded buffering when no handler
    // consumes the body (e.g. unmatched routes). Pulled from the owning
    // server (IHttpProcessor::GetMaxRequestBodySize) at construction.
    uint64_t max_request_body_size_ = 16 * 1024 * 1024;

    // P1-H6d (RFC 9114 §4.1 / RFC 9110 §6.5): message phase for field
    // sections. The FIRST field section builds the request and routes; any
    // LATER field section on the same stream is trailers — appended to the
    // request, never re-routed, and pseudo-headers in it are a protocol
    // error (RFC 9114 §4.3: pseudo-header fields only appear in the first
    // section).
    bool saw_initial_field_section_ = false;
    // P1-H6d (RFC 9114 §4.1): set when the trailers section arrived. The
    // trailing field section MUST be the final frame of the message — any
    // DATA frame after it is malformed and errors the stream.
    bool saw_trailers_ = false;

    uint64_t body_length_ = 0;
    // P1-H6c: true when the request declared a content-length (any value).
    // Distinguishes "no body declared, none arrived" (legal) from
    // "content-length: 0 but DATA arrived" and "content-length: N but the
    // stream FINed short" (both H3_MESSAGE_ERROR, RFC 9110 §8.6 smuggling /
    // truncation vectors).
    bool has_content_length_ = false;
    uint32_t received_body_length_;
    bool is_response_sent_;
    RouteConfig route_config_;
    std::shared_ptr<Request> request_;    // Request object (created in HandleHeaders)
    std::shared_ptr<Response> response_;  // Response object (created in HandleHeaders, used in HandleData)
    std::weak_ptr<IHttpProcessor> http_processor_;
    std::function<void(std::shared_ptr<IResponse>, std::shared_ptr<ResponseStream>)> push_handler_;
    std::function<bool()> settings_received_cb_;  // RFC 9114: Check if peer SETTINGS received
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_STREAM_RESPONSE_STREAM
