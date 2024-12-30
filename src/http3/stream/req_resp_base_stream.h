#ifndef HTTP3_STREAM_REQ_RESP_BASE_STREAM
#define HTTP3_STREAM_REQ_RESP_BASE_STREAM

#include <memory>
#include <unordered_map>
#include "http3/frame/if_frame.h"
#include "http3/stream/if_stream.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace http3 {

class ReqRespBaseStream:
    public IStream {
public:
    ReqRespBaseStream(std::shared_ptr<QpackEncoder> qpack_encoder,
        std::shared_ptr<quic::IQuicBidirectionStream> stream);
    virtual ~ReqRespBaseStream();
    virtual StreamType GetType() override { return ST_REQ_RESP; }

protected:
    virtual void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error);
    virtual void HandleHeaders(std::shared_ptr<IFrame> frame);
    virtual void HandleData(std::shared_ptr<IFrame> frame);

    virtual void HandleFrame(std::shared_ptr<IFrame> frame) = 0;
    virtual void HandleBody() = 0;

protected:
    uint32_t body_length_;
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::IQuicBidirectionStream> stream_;

    // request or response
    std::unordered_map<std::string, std::string> headers_;
    std::vector<uint8_t> body_;
};

}
}

#endif
