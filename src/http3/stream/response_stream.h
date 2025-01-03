#ifndef HTTP3_STREAM_RESPONSE_STREAM
#define HTTP3_STREAM_RESPONSE_STREAM

#include <string>
#include <memory>
#include <unordered_map>

#include "http3/include/type.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/req_resp_base_stream.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace http3 {

class ResponseStream:
    public ReqRespBaseStream {
public:
    ResponseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<quic::IQuicBidirectionStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const http_handler& http_handler);
    virtual ~ResponseStream();

    void SendPushPromise(const std::unordered_map<std::string, std::string>& headers, int32_t push_id);
    void SendResponse(const std::shared_ptr<IResponse> response);
private:
    virtual void HandleBody() override;

private:
    http_handler http_handler_;
};

}
}

#endif
