#ifndef HTTP3_STREAM_REQUEST_STREAM
#define HTTP3_STREAM_REQUEST_STREAM

#include <memory>
#include "http3/stream/if_stream.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace http3 {

class RequestStream:
    public IStream {
public:
    RequestStream(std::shared_ptr<QpackEncoder> qpack_encoder,
        std::shared_ptr<quic::IQuicBidirectionStream> stream);
    virtual ~RequestStream();

    // Implement IStream interface
    virtual StreamType GetType() override { return ST_REQ_RESP; }

    // Send request/response
    bool SendRequest(const IRequest& request, const http_response_handler& handler);

private:
    void OnResponse(std::shared_ptr<common::IBufferRead> data, uint32_t error);

private:
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::IQuicBidirectionStream> stream_;
    http_response_handler handler_;
};

}
}

#endif
