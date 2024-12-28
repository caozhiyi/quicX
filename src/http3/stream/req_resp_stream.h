#ifndef HTTP3_STREAM_REQ_RESP_STREAM
#define HTTP3_STREAM_REQ_RESP_STREAM

#include <memory>
#include "http3/stream/if_stream.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace http3 {

class ReqRespStream:
    public IStream {
public:
    ReqRespStream(std::shared_ptr<QpackEncoder> qpack_encoder,
        std::shared_ptr<quic::IQuicBidirectionStream> stream);
    virtual ~ReqRespStream();

    // Implement IStream interface
    virtual StreamType GetType() override { return ST_REQ_RESP; }

    // Send request/response
    bool SendRequest(std::shared_ptr<IRequest> request);
    bool SendResponse(std::shared_ptr<IResponse> response);

private:
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::IQuicBidirectionStream> stream_;
};

}
}

#endif
