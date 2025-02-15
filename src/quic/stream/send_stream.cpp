#include "common/log/log.h"
#include "quic/connection/error.h"
#include "quic/stream/send_stream.h"
#include "quic/frame/stream_frame.h"
#include "common/alloter/if_alloter.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chains.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/state_machine_send.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {
namespace quic {

SendStream::SendStream(std::shared_ptr<common::BlockMemoryPool>& alloter,
    uint64_t init_data_limit,
    uint64_t id,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    IStream(id, active_send_cb, stream_close_cb, connection_close_cb),
    to_fin_(false),
    send_data_offset_(0),
    peer_data_limit_(init_data_limit),
    send_buffer_(std::make_shared<common::BufferChains>(alloter)) {
    send_machine_ = std::make_shared<StreamStateMachineSend>(std::bind(&SendStream::Close, this));
}

SendStream::~SendStream() {

}

void SendStream::Close() {
    to_fin_ = true;
    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
}

void SendStream::Reset(uint32_t error) {
    // check status
    if(!send_machine_->OnFrame(FrameType::kResetStream)) {
        return;
    }

    auto frame = std::make_shared<ResetStreamFrame>();
    frame->SetStreamID(stream_id_);
    frame->SetFinalSize(send_data_offset_);
    frame->SetAppErrorCode(error);

    frames_list_.emplace_back(frame);
}

int32_t SendStream::Send(uint8_t* data, uint32_t len) {
    if (!send_machine_->CanSendAppData()) {
        return -1;
    }
    
    int32_t ret = send_buffer_->Write(data, len);
    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
    return ret;
}

int32_t SendStream::Send(std::shared_ptr<common::IBufferRead> buffer) {
    if (!send_machine_->CanSendAppData()) {
        return -1;
    }
    
    int32_t ret = send_buffer_->Write(buffer->GetData(), buffer->GetDataLength());
    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
    return ret;
}

uint32_t SendStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    switch (frame_type)
    {
    case FrameType::kMaxStreamData:
        OnMaxStreamDataFrame(frame);
        break;
    case FrameType::kStopSending:
        OnStopSendingFrame(frame);
        break;
    default:
        common::LOG_ERROR("unexcept frame on send stream. frame type:%d", frame_type);
    }
    return 0;
}

IStream::TrySendResult SendStream::TrySendData(IFrameVisitor* visitor) {
    IStream::TrySendData(nullptr);

    // check peer limit 
    // TODO put number to config
    if (peer_data_limit_ - send_data_offset_ < 2048) {
        if (send_machine_->CanSendDataBlockFrame()) {
            // make stream block frame
            std::shared_ptr<StreamDataBlockedFrame> frame = std::make_shared<StreamDataBlockedFrame>();
            frame->SetStreamID(stream_id_);
            frame->SetMaximumData(peer_data_limit_);
    
            if (!visitor->HandleFrame(frame)) {
                return TrySendResult::kFailed;
            }
        }
    }

    if (peer_data_limit_ <= send_data_offset_) {
        return TrySendResult::kFailed;
    }
    
    for (auto iter = frames_list_.begin(); iter != frames_list_.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = frames_list_.erase(iter);

        } else {
            return TrySendResult::kFailed;
        }
    }

    if (!send_machine_->CanSendStrameFrame()) {
        return TrySendResult::kSuccess;
    }

    // make stream frame
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(stream_id_);
    frame->SetOffset(send_data_offset_);
    if (to_fin_) {
        frame->SetFin();
    }
    
    uint32_t stream_send_size = peer_data_limit_ - send_data_offset_;
    uint32_t conn_send_size = visitor->GetLeftStreamDataSize();
    uint32_t send_size = stream_send_size > conn_send_size ? conn_send_size : stream_send_size;
    send_size = send_size > 1000 ? 1000 : send_size;

    // TODO not copy buffer
    uint8_t buf[1000] = {0};
    uint32_t size = send_buffer_->ReadNotMovePt(buf, send_size);
    frame->SetData(buf, size);

    if (!visitor->HandleFrame(frame)) {
        return TrySendResult::kFailed;
    }
    visitor->AddStreamDataSize(send_size);

    send_buffer_->MoveReadPt(size);
    send_data_offset_ += size;

    if (sended_cb_) {
        sended_cb_(size, 0);
    }
    return TrySendResult::kSuccess;
}

void SendStream::OnMaxStreamDataFrame(std::shared_ptr<IFrame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxStreamDataFrame>(frame);
    uint64_t new_limit = max_data_frame->GetMaximumData();

    if (new_limit <= peer_data_limit_) {
        common::LOG_WARN("get a invalid max data stream. new limit:%d, current limit:%d", new_limit, peer_data_limit_);
        return;
    }

    uint32_t can_write_size = new_limit - peer_data_limit_;
    peer_data_limit_ = new_limit;

    if (send_buffer_->GetDataLength() > 0) {
        ToSend();
    }
}

void SendStream::OnStopSendingFrame(std::shared_ptr<IFrame> frame) {
    auto stop_frame = std::dynamic_pointer_cast<StopSendingFrame>(frame);
    uint32_t err = stop_frame->GetAppErrorCode();

    if (send_machine_->CanSendResetStreamFrame()) {
        Reset(err);
    }

    if (sended_cb_) {
        sended_cb_(0, err);
    }
}

}
}
