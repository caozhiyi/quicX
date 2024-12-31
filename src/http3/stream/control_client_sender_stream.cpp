#include "common/buffer/buffer.h"
#include "http3/frame/max_push_id_frame.h"
#include "http3/frame/cancel_push_frame.h"
#include "http3/stream/control_client_sender_stream.h"

namespace quicx {
namespace http3 {

ControlClientSenderStream::ControlClientSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
    const std::function<void(uint64_t id, int32_t error)>& error_handler): 
    ControlSenderStream(stream, error_handler) {
}

ControlClientSenderStream::~ControlClientSenderStream() {

}

bool ControlClientSenderStream::SendMaxPushId(uint64_t push_id) {
    MaxPushIdFrame frame;
    frame.SetPushId(push_id);

    uint8_t buf[64]; // TODO: Use dynamic buffer
    auto buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));
    if (!frame.Encode(buffer)) {
        return false;
    }
    return stream_->Send(buffer) > 0;
}

bool ControlClientSenderStream::SendCancelPush(uint64_t push_id) {
    CancelPushFrame frame;
    frame.SetPushId(push_id);

    uint8_t buf[64]; // TODO: Use dynamic buffer
    auto buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));
    if (!frame.Encode(buffer)) {
        return false;
    }
    return stream_->Send(buffer) > 0;
}

}
}
