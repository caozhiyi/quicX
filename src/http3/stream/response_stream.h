#ifndef HTTP3_STREAM_RESPONSE_STREAM
#define HTTP3_STREAM_RESPONSE_STREAM

#include <memory>
#include "http3/include/type.h"
#include "http3/stream/if_stream.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace http3 {

class ResponseStream:
    public IStream {
public:
    ResponseStream(std::shared_ptr<QpackEncoder> qpack_encoder,
        std::shared_ptr<quic::IQuicBidirectionStream> stream,
        http_handler handler);
    virtual ~ResponseStream();

    // Implement IStream interface
    virtual StreamType GetType() override { return ST_REQ_RESP; }

private:
    void OnRequest(std::shared_ptr<common::IBufferRead> data, uint32_t error);

private:
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::IQuicBidirectionStream> stream_;
    http_handler handler_;
};

}
}

#endif
