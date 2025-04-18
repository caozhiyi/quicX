#ifndef HTTP3_STREAM_CONTROL_CLIENT_SENDER_STREAM
#define HTTP3_STREAM_CONTROL_CLIENT_SENDER_STREAM

#include "http3/stream/control_sender_stream.h"

namespace quicx {
namespace http3 {

class ControlClientSenderStream:
    public ControlSenderStream {
public:
    ControlClientSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    virtual ~ControlClientSenderStream();

    virtual StreamType GetType() override { return StreamType::kControl; }
    // Send MAX_PUSH_ID frame
    virtual bool SendMaxPushId(uint64_t push_id);

    // Send CANCEL_PUSH frame
    virtual bool SendCancelPush(uint64_t push_id);
};

}
}

#endif
