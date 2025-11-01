#include <memory>
#include "http3/frame/max_push_id_frame.h"
#include "http3/frame/cancel_push_frame.h"
#include "http3/stream/control_server_receiver_stream.h"

namespace quicx {
namespace http3 {

ControlServerReceiverStream::ControlServerReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const std::function<void(uint64_t id)>& goaway_handler,
        const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler,
        const std::function<void(uint64_t push_id)>& max_push_id_handler,
        const std::function<void(uint64_t id)>& cancel_handler):
    ControlReceiverStream(stream, error_handler, goaway_handler, settings_handler),
    max_push_id_handler_(max_push_id_handler),
    cancel_handler_(cancel_handler) {

}

ControlServerReceiverStream::~ControlServerReceiverStream() {

}

void ControlServerReceiverStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    switch (frame->GetType()) {
        case FrameType::kMaxPushId: {
            auto max_push_id_frame = std::dynamic_pointer_cast<MaxPushIdFrame>(frame);
            max_push_id_handler_(max_push_id_frame->GetPushId());
            break;
        }
        case FrameType::kCancelPush: {
            auto cancel_push_frame = std::dynamic_pointer_cast<CancelPushFrame>(frame);
            cancel_handler_(cancel_push_frame->GetPushId());
            break;
        }

        default:
            ControlReceiverStream::HandleFrame(frame);
            break;
    }
}

}
}
