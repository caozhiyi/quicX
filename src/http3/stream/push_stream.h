#ifndef HTTP3_STREAM_PUSH_STREAM
#define HTTP3_STREAM_PUSH_STREAM

#include <memory>
#include <string>
#include <unordered_map>

#include "http3/stream/if_stream.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

class PushStream:
    public IStream {
public:
    PushStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<quic::IQuicSendStream>& stream,
        const std::function<void(int32_t)>& error_handler,
        uint64_t push_id);
    virtual ~PushStream();

    // Implement IStream interface
    virtual StreamType GetType() override { return ST_PUSH; }

    // Send push response headers and data
    bool SendPushResponse(const std::unordered_map<std::string, std::string>& headers,
                         const std::string& body = "");

private:
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::IQuicSendStream> stream_;
    uint64_t push_id_;
};

}
}

#endif
