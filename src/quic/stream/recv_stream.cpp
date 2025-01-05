#include "common/log/log.h"
#include "quic/connection/error.h"
#include "quic/stream/recv_stream.h"
#include "quic/frame/stream_frame.h"
#include "common/buffer/buffer_chains.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/state_machine_recv.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {
namespace quic {

RecvStream::RecvStream(std::shared_ptr<common::BlockMemoryPool>& alloter,
    uint64_t init_data_limit,
    uint64_t id,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    IStream(id, active_send_cb, stream_close_cb, connection_close_cb),
    local_data_limit_(init_data_limit),
    final_offset_(0),
    except_offset_(0) {
    buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
    recv_machine_ = std::make_shared<StreamStateMachineRecv>(std::bind(&RecvStream::Reset, this, 0)); // TODO make error code
}

RecvStream::~RecvStream() {

}

void RecvStream::Reset(uint32_t error) {
    if (recv_machine_->CanSendStopSendingFrame()) {
        auto stop_frame = std::make_shared<StopSendingFrame>();
        stop_frame->SetStreamID(stream_id_);
        stop_frame->SetAppErrorCode(error);

        frames_list_.emplace_back(stop_frame);
        ToSend();
    }
}

uint32_t RecvStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    switch (frame_type)
    {
    case FT_STREAM_DATA_BLOCKED:
        OnStreamDataBlockFrame(frame);
        break;
    case FT_RESET_STREAM:
        OnResetStreamFrame(frame);
        break;
    default:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            return OnStreamFrame(frame);
        } else {
            common::LOG_ERROR("unexcept frame on recv stream. frame type:%d", frame_type);
        }
    }
    return 0;
}

IStream::TrySendResult RecvStream::TrySendData(IFrameVisitor* visitor) {
    IStream::TrySendData(nullptr);

    for (auto iter = frames_list_.begin(); iter != frames_list_.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = frames_list_.erase(iter);

        } else {
            return TSR_FAILED;
        }
    }
    return TSR_SUCCESS;
}

uint32_t RecvStream::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    if(!recv_machine_->OnFrame(frame->GetType())) {
        return 0;
    }

    // check flow control
    auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
    if (stream_frame->GetOffset() + stream_frame->GetLength() > local_data_limit_) {
        if (connection_close_cb_) {
            connection_close_cb_(QEC_FLOW_CONTROL_ERROR, frame->GetType(), "stream recv data exceeding flow control limits.");
        }
        return 0;
    }
    
    // check stream frame whit fin
    if (stream_frame->IsFin()) {
        uint64_t fin_offset = stream_frame->GetOffset() + stream_frame->GetLength();
        if (final_offset_ != 0 && fin_offset != final_offset_) {
            common::LOG_ERROR("invalid final size. size:%d", fin_offset);
            if (connection_close_cb_) {
                connection_close_cb_(QEC_FINAL_SIZE_ERROR, frame->GetType(), "final size change.");
            }
            return 0;
        }
        final_offset_ = fin_offset;
    }
    

    if (stream_frame->GetOffset() == except_offset_) {
        buffer_->Write(stream_frame->GetData(), stream_frame->GetLength());
        except_offset_ += stream_frame->GetLength();

        while (true) {
            auto iter = out_order_frame_.find(except_offset_);
            if (iter == out_order_frame_.end()) {
                break;
            }

            stream_frame = std::dynamic_pointer_cast<StreamFrame>(iter->second);
            buffer_->Write(stream_frame->GetData(), stream_frame->GetLength());
            except_offset_ += stream_frame->GetLength();
            out_order_frame_.erase(iter);
        }
        
        if (final_offset_ != 0 && final_offset_ == except_offset_ && out_order_frame_.empty()) {
            recv_machine_->RecvAllData();
        }

        if (recv_cb_) {
            recv_cb_(buffer_, 100); // TODO make error code. close
        }

        if (recv_machine_->CanAppReadAllData()) {
            recv_machine_->AppReadAllData();
        }

    } else {
        // recv a repeat packet
        if (out_order_frame_.find(stream_frame->GetOffset()) != out_order_frame_.end() ||
            stream_frame->GetOffset() < except_offset_) {
            return 0;
        }
        
        out_order_frame_[stream_frame->GetOffset()] = stream_frame;
    }

    // TODO put number to config
    if (local_data_limit_ - except_offset_ < 3096) {
        if (recv_machine_->CanSendMaxStrameDataFrame()) {
            local_data_limit_ += 3096;
            auto max_frame = std::make_shared<MaxStreamDataFrame>();
            max_frame->SetStreamID(stream_id_);
            max_frame->SetMaximumData(local_data_limit_);
    
            ToSend();
        }
    }
    return stream_frame->GetLength();
}

void RecvStream::OnStreamDataBlockFrame(std::shared_ptr<IFrame> frame) {
    if(!recv_machine_->OnFrame(frame->GetType())) {
        return;
    }
    
    auto block_frame = std::dynamic_pointer_cast<StreamDataBlockedFrame>(frame);
    common::LOG_WARN("peer send block. offset:%d", block_frame->GetMaximumData());

    auto max_frame = std::make_shared<MaxStreamDataFrame>();
    max_frame->SetStreamID(stream_id_);
    max_frame->SetMaximumData(local_data_limit_ + 3096); // TODO. define increase steps
    frames_list_.emplace_back(max_frame);

    ToSend();
}

void RecvStream::OnResetStreamFrame(std::shared_ptr<IFrame> frame) {
    if(!recv_machine_->OnFrame(frame->GetType())) {
        return;
    }
    
    auto reset_frame = std::dynamic_pointer_cast<ResetStreamFrame>(frame);
    uint64_t fin_offset = reset_frame->GetFinalSize();

    if (final_offset_ != 0 && fin_offset != final_offset_) {
        common::LOG_ERROR("invalid final size. size:%d", fin_offset);
        if (connection_close_cb_) {
            connection_close_cb_(QEC_FINAL_SIZE_ERROR, frame->GetType(), "final size change.");
        }
        return;
    }

    final_offset_ = fin_offset;

    // TODO delay call it
    if (recv_cb_) {
        recv_cb_(buffer_, reset_frame->GetAppErrorCode());
    }
}

}
}
