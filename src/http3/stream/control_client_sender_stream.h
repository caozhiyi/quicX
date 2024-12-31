#ifndef HTTP3_STREAM_CONTROL_CLIENT_SENDER_STREAM
#define HTTP3_STREAM_CONTROL_CLIENT_SENDER_STREAM

#include "http3/stream/control_sender_stream.h"

namespace quicx {
namespace http3 {

class ControlClientSenderStream:
    public ControlSenderStream {
public:
    ControlClientSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
        const std::function<void(int32_t)>& error_handler);
    virtual ~ControlClientSenderStream();

    virtual StreamType GetType() { return StreamType::ST_CONTROL; }

    // Send MAX_PUSH_ID frame
    virtual bool SendMaxPushId(uint64_t push_id);

    // Send CANCEL_PUSH frame
    virtual bool SendCancelPush(uint64_t push_id);
};

}
}

#endif
