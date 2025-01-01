#ifndef HTTP3_STREAM_PUSH_RECEIVER_STREAM
#define HTTP3_STREAM_PUSH_RECEIVER_STREAM

#include <memory>
#include <string>
#include <unordered_map>

#include "http3/include/type.h"
#include "http3/frame/if_frame.h"
#include "http3/stream/if_stream.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_recv_stream.h"

namespace quicx {
namespace http3 {

class PushReceiverStream:
    public IStream {
public:
    PushReceiverStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<quic::IQuicRecvStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const http_response_handler& response_handler);
    virtual ~PushReceiverStream();

    // Implement IStream interface
    virtual StreamType GetType() override { return ST_PUSH; }

    virtual uint64_t GetStreamID() { return stream_->GetStreamID(); }

private:
    virtual void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error);
    virtual void HandleHeaders(std::shared_ptr<IFrame> frame);
    virtual void HandleData(std::shared_ptr<IFrame> frame);
    virtual void HandleBody();


private:
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::IQuicRecvStream> stream_;

    http_response_handler response_handler_;
    
    uint32_t body_length_;
    std::vector<uint8_t> body_;
    std::unordered_map<std::string, std::string> headers_;
};

}
}

#endif
