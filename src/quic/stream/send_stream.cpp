#include "common/log/log.h"
#include "quic/stream/send_stream.h"
#include "quic/frame/stream_frame.h"
#include "common/alloter/pool_block.h"
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
    acked_offset_(0),
    fin_sent_(false),
    peer_data_limit_(init_data_limit),
    send_buffer_(std::make_shared<common::MultiBlockBuffer>(alloter)) {
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
    common::LOG_DEBUG("stream send reset stream. stream id:%d, error:%d", stream_id_, error);
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

int32_t SendStream::Send(std::shared_ptr<IBufferRead> buffer) {
    if (!send_machine_->CanSendAppData()) {
        return -1;
    }
    
    int32_t ret = send_buffer_->Write(buffer);
    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
    return ret;
}

std::shared_ptr<IBufferWrite> SendStream::GetSendBuffer() {
    return std::dynamic_pointer_cast<IBufferWrite>(send_buffer_);
}

bool SendStream::Flush() {
    if (!send_machine_->CanSendAppData()) {
        return false;
    }

    if (send_buffer_->GetDataLength() == 0) {
        return false;
    }

    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
    return true;
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
            common::LOG_DEBUG("stream send data blocked. stream id:%d, peer data limit:%d", stream_id_, peer_data_limit_);
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
        fin_sent_ = true;  // Mark that FIN has been sent
    }
    
    uint32_t stream_send_size = peer_data_limit_ - send_data_offset_;
    uint32_t conn_send_size = visitor->GetLeftStreamDataSize();
    uint32_t send_size = stream_send_size > conn_send_size ? conn_send_size : stream_send_size;
    send_size = send_size > 1000 ? 1000 : send_size;

    common::SharedBufferSpan data = send_buffer_->GetSharedBufferSpan(send_size);
    frame->SetData(data);

    if (!send_machine_->OnFrame(frame->GetType())) {
        common::LOG_WARN("stream state transition rejected. stream id:%d, frame type:%d, state=%d", stream_id_, frame->GetType(), static_cast<int>(send_machine_->GetStatus()));
    }

    if (!visitor->HandleFrame(frame)) {
        return TrySendResult::kFailed;
    }
    visitor->AddStreamDataSize(data.GetLength());
    common::LOG_DEBUG("stream send data. stream id:%d, send size:%d", stream_id_, data.GetLength());

    send_buffer_->MoveReadPt(data.GetLength());
    send_data_offset_ += data.GetLength();

    if (sended_cb_) {
        sended_cb_(data.GetLength(), 0);
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
    common::LOG_DEBUG("stream recv max stream data. stream id:%d, new limit:%d", stream_id_, new_limit);
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
    common::LOG_DEBUG("stream recv stop sending. stream id:%d, error:%d", stream_id_, err);
}

void SendStream::OnDataAcked(uint64_t max_offset, bool has_fin) {
    common::LOG_DEBUG("SendStream::OnDataAcked: stream_id=%d, max_offset=%llu, has_fin=%d, current acked_offset=%llu",
                     stream_id_, max_offset, has_fin, acked_offset_);
    
    // Update to maximum acked offset
    if (max_offset > acked_offset_) {
        acked_offset_ = max_offset;
    }
    
    // If FIN was acked, mark it
    if (has_fin) {
        fin_sent_ = true;  // Ensure fin_sent_ is set
    }
    
    // Check if all data has been ACKed
    CheckAllDataAcked();
}

void SendStream::CheckAllDataAcked() {
    // Check if all data (including FIN) has been ACKed
    // Condition: fin_sent_ && acked_offset_ >= send_data_offset_
    if (fin_sent_ && acked_offset_ >= send_data_offset_) {
        common::LOG_DEBUG("SendStream::CheckAllDataAcked: all data acked for stream %d, transitioning to Data Recvd state",
                         stream_id_);
        
        // Transition to terminal state (Data Recvd)
        if (send_machine_->AllAckDone()) {
            // For bidirectional streams, this will be handled by CheckStreamClose
            // For unidirectional send streams, the stream can be closed now
            common::LOG_DEBUG("SendStream: stream %d reached Data Recvd state", stream_id_);
        }
    }
}

}
}
