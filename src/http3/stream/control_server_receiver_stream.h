#ifndef HTTP3_STREAM_CONTROL_RECEIVER_RECEIVER_STREAM
#define HTTP3_STREAM_CONTROL_RECEIVER_RECEIVER_STREAM

#include "http3/frame/if_frame.h"
#include "http3/stream/control_receiver_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief Control server receiver stream
 *
 * The control server receiver stream is used to receive control frames from the client.
 */
class ControlServerReceiverStream: public ControlReceiverStream {
public:
    ControlServerReceiverStream(const std::shared_ptr<IQuicRecvStream>& stream,
        const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const std::function<void(uint64_t id)>& goaway_handler,
        const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler,
        const std::function<void(uint64_t push_id)>& max_push_id_handler,
        const std::function<void(uint64_t id)>& cancel_handler);
    virtual ~ControlServerReceiverStream();

private:
    virtual void HandleFrame(std::shared_ptr<IFrame> frame);

private:
    std::function<void(uint64_t push_id)> max_push_id_handler_;
    std::function<void(uint64_t id)> cancel_handler_;

    // RFC 9114: MAX_PUSH_ID must be monotonically increasing
    uint64_t last_max_push_id_ = 0;
    bool max_push_id_received_ = false;

    // RFC 9114: GOAWAY stream ID must not increase
    uint64_t last_goaway_id_ = UINT64_MAX;
    bool goaway_received_ = false;
};

}  // namespace http3
}  // namespace quicx

#endif
