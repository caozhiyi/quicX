#ifndef HTTP3_STREAM_REQUEST_STREAM
#define HTTP3_STREAM_REQUEST_STREAM

#include <memory>
#include <unordered_map>

#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/req_resp_base_stream.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace http3 {

class RequestStream:
    public ReqRespBaseStream {
public:
    RequestStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<quic::IQuicBidirectionStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const http_response_handler& response_handler,
        const std::function<void(std::unordered_map<std::string, std::string>&)>& push_promise_handler);
    virtual ~RequestStream();

    // Send request/response
    bool SendRequest(std::shared_ptr<IRequest> request);

private:
    virtual void HandleFrame(std::shared_ptr<IFrame> frame) override;
    virtual void HandleBody() override;
    void HandlePushPromise(std::shared_ptr<IFrame> frame);

private:
    http_response_handler response_handler_;
    std::function<void(std::unordered_map<std::string, std::string>&)> push_promise_handler_;
};

}
}

#endif
