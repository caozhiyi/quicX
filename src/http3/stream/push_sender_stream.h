#ifndef HTTP3_STREAM_PUSH_SENDER_STREAM
#define HTTP3_STREAM_PUSH_SENDER_STREAM

#include <memory>
#include <string>
#include <unordered_map>

#include "http3/stream/if_stream.h"
#include "http3/include/if_response.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

class PushSenderStream:
    public IStream {
public:
    PushSenderStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<quic::IQuicSendStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    virtual ~PushSenderStream();

    // Implement IStream interface
    virtual StreamType GetType() override { return StreamType::kPush; }
    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }

    // Send push response headers and data (RFC 9114 Section 4.6)
    // Push stream format: Stream Type (0x01) + Push ID (varint) + HTTP Message
    bool SendPushResponse(uint64_t push_id, std::shared_ptr<IResponse> response);

    // Reset the push stream (used when client cancels push)
    // RFC 9114 Section 7.2.3: Server MUST stop sending push stream when CANCEL_PUSH is received
    void Reset(uint32_t error_code);

    void SetPushId(uint64_t push_id) { push_id_ = push_id; }
    uint64_t GetPushId() const { return push_id_; }

private:
    uint64_t push_id_ = 0;
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::IQuicSendStream> stream_;
};

}
}

#endif
