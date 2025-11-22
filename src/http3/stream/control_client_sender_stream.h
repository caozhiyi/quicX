#ifndef HTTP3_STREAM_CONTROL_CLIENT_SENDER_STREAM
#define HTTP3_STREAM_CONTROL_CLIENT_SENDER_STREAM

#include "http3/stream/control_sender_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief ControlClientSenderStream is the client control sender stream
 *
 * The client control sender stream is used to send control frames to the server.
 * It is responsible for sending the MAX_PUSH_ID and CANCEL_PUSH frames to the server.
 */
class ControlClientSenderStream: public ControlSenderStream {
public:
    ControlClientSenderStream(const std::shared_ptr<IQuicSendStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    virtual ~ControlClientSenderStream();

    virtual StreamType GetType() override { return StreamType::kControl; }
    // Send MAX_PUSH_ID frame
    virtual bool SendMaxPushId(uint64_t push_id);

    // Send CANCEL_PUSH frame
    virtual bool SendCancelPush(uint64_t push_id);
};

}  // namespace http3
}  // namespace quicx

#endif
