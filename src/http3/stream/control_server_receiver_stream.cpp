#include <memory>
#include <cstdint>
#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/max_push_id_frame.h"
#include "http3/frame/cancel_push_frame.h"
#include "http3/stream/control_server_receiver_stream.h"

namespace quicx {
namespace http3 {

ControlServerReceiverStream::ControlServerReceiverStream(const std::shared_ptr<IQuicRecvStream>& stream,
    const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(uint64_t id)>& goaway_handler,
    const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler,
    const std::function<void(uint64_t push_id)>& max_push_id_handler,
    const std::function<void(uint64_t id)>& cancel_handler):
    ControlReceiverStream(stream, qpack_encoder, error_handler, goaway_handler, settings_handler),
    max_push_id_handler_(max_push_id_handler),
    cancel_handler_(cancel_handler) {}

ControlServerReceiverStream::~ControlServerReceiverStream() {}

void ControlServerReceiverStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    switch (frame->GetType()) {
        case FrameType::kGoAway: {
            auto goaway_frame = std::static_pointer_cast<GoAwayFrame>(frame);
            uint64_t stream_id = goaway_frame->GetStreamId();

            // RFC 9114 Section 5.2: A server MUST NOT send a GOAWAY with a stream ID
            // that is greater than the stream ID given in an earlier GOAWAY
            if (goaway_received_ && stream_id > last_goaway_id_) {
                common::LOG_ERROR("ControlServerReceiverStream: GOAWAY stream ID increased from %llu to %llu",
                    last_goaway_id_, stream_id);
                error_handler_(stream_->GetStreamID(), Http3ErrorCode::kIdError);
                return;
            }

            goaway_received_ = true;
            last_goaway_id_ = stream_id;

            // Call the base class handler which will invoke goaway_handler_
            ControlReceiverStream::HandleFrame(frame);
            break;
        }
        case FrameType::kMaxPushId: {
            auto max_push_id_frame = std::dynamic_pointer_cast<MaxPushIdFrame>(frame);
            uint64_t push_id = max_push_id_frame->GetPushId();

            // RFC 9114 Section 7.2.7: MAX_PUSH_ID value MUST NOT be reduced
            if (max_push_id_received_ && push_id < last_max_push_id_) {
                common::LOG_ERROR(
                    "ControlServerReceiverStream: MAX_PUSH_ID decreased from %llu to %llu", last_max_push_id_, push_id);
                error_handler_(stream_->GetStreamID(), Http3ErrorCode::kIdError);
                return;
            }

            max_push_id_received_ = true;
            last_max_push_id_ = push_id;
            max_push_id_handler_(push_id);
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

}  // namespace http3
}  // namespace quicx
