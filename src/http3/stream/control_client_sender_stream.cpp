#include "common/log/log.h"
#include "http3/http/error.h"
#include "common/buffer/if_buffer.h"
#include "http3/frame/max_push_id_frame.h"
#include "http3/frame/cancel_push_frame.h"
#include "http3/stream/control_client_sender_stream.h"

namespace quicx {
namespace http3 {

ControlClientSenderStream::ControlClientSenderStream(const std::shared_ptr<IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler): 
    ControlSenderStream(stream, error_handler) {
}

ControlClientSenderStream::~ControlClientSenderStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool ControlClientSenderStream::SendMaxPushId(uint64_t push_id) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    
    MaxPushIdFrame frame;
    frame.SetPushId(push_id);

    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!frame.Encode(buffer)) {
        common::LOG_ERROR("ControlClientSenderStream::SendMaxPushId: Failed to encode MaxPushIdFrame");
        error_handler_(0, Http3ErrorCode::kMessageError);
        return false;
    }
    common::LOG_DEBUG("ControlClientSenderStream::SendMaxPushId: max_push_id=%llu, buffer length=%llu", push_id, buffer->GetDataLength());
    return stream_->Flush();
}

bool ControlClientSenderStream::SendCancelPush(uint64_t push_id) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    
    CancelPushFrame frame;
    frame.SetPushId(push_id);

    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!frame.Encode(buffer)) {
        common::LOG_ERROR("ControlClientSenderStream::SendCancelPush: Failed to encode CancelPushFrame");
        error_handler_(0, Http3ErrorCode::kInternalError);
        return false;
    }
    return stream_->Send(buffer) > 0;
}

}
}
