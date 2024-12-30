#ifndef HTTP3_STREAM_REQUEST_STREAM
#define HTTP3_STREAM_REQUEST_STREAM

#include <memory>
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
    RequestStream(std::shared_ptr<QpackEncoder> qpack_encoder,
        std::shared_ptr<quic::IQuicBidirectionStream> stream);
    virtual ~RequestStream();

    // Send request/response
    bool SendRequest(const IRequest& request, const http_response_handler& handler);

private:
    virtual void HandleFrame(std::shared_ptr<IFrame> frame) override;
    virtual void HandleBody() override;
    void HandlePushPromise(std::shared_ptr<IFrame> frame);

private:
    http_response_handler handler_;
};

}
}

#endif
