#ifndef HTTP3_STREAM_CONTROL_RECEIVER_RECEIVER_STREAM
#define HTTP3_STREAM_CONTROL_RECEIVER_RECEIVER_STREAM

#include "http3/frame/if_frame.h"
#include "http3/stream/control_receiver_stream.h"

namespace quicx {
namespace http3 {

class ControlServerReceiverStream:
    public ControlReceiverStream {
public:
    ControlServerReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
        const std::function<void(int32_t)>& error_handler,
        const std::function<void(uint64_t id)>& goaway_handler,
        const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler,
        const std::function<void(uint64_t push_id)>& max_push_id_handler,
        const std::function<void(uint64_t id)>& cancel_handler);
    virtual ~ControlServerReceiverStream();

    virtual StreamType GetType() { return StreamType::ST_CONTROL; }
private:
    virtual void HandleFrame(std::shared_ptr<IFrame> frame);

private:
    std::function<void(uint64_t push_id)> max_push_id_handler_;
    std::function<void(uint64_t id)> cancel_handler_;
    
};

}
}

#endif
