#ifndef HTTP3_STREAM_PUSH_RECEIVER_STREAM
#define HTTP3_STREAM_PUSH_RECEIVER_STREAM

#include <memory>
#include <string>
#include <unordered_map>

#include "http3/include/type.h"
#include "http3/frame/if_frame.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/if_recv_stream.h"
#include "quic/include/if_quic_recv_stream.h"

namespace quicx {
namespace http3 {

class PushReceiverStream:
    public IRecvStream {
public:
    PushReceiverStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<quic::IQuicRecvStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const http_response_handler& response_handler);
    virtual ~PushReceiverStream();

    virtual void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) override;

private:
    virtual void HandleHeaders(std::shared_ptr<IFrame> frame);
    virtual void HandleData(std::shared_ptr<IFrame> frame);
    virtual void HandleBody();

private:
    enum class ParseState {
        kReadingPushId,      // Reading push ID (stream type already read by UnidentifiedStream)
        kReadingFrames       // Reading HTTP3 frames (HEADERS + DATA)
    };

    std::shared_ptr<QpackEncoder> qpack_encoder_;

    http_response_handler response_handler_;
    
    ParseState parse_state_;
    uint64_t push_id_;
    std::vector<uint8_t> buffer_;  // Buffer for incomplete data
    
    uint32_t body_length_;
    std::vector<uint8_t> body_;
    std::unordered_map<std::string, std::string> headers_;
};

}
}

#endif
