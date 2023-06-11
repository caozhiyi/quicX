#include "common/log/log.h"
#include "quic/stream/recv_stream.h"
#include "quic/frame/stream_frame.h"
#include "common/buffer/buffer_chains.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/recv_state_machine.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/connection/connection_interface.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {

RecvStream::RecvStream(std::shared_ptr<BlockMemoryPool>& alloter, uint64_t id):
    IRecvStream(id),
    _local_data_limit(0),
    _final_offset(0),
    _except_offset(0) {
    _recv_machine = std::shared_ptr<RecvStreamStateMachine>();
    _recv_buffer = std::make_shared<BufferChains>(alloter);

}

RecvStream::~RecvStream() {

}

void RecvStream::Close(uint64_t error) {
    if (_recv_machine->CanSendStopSendingFrame()) {
        auto stop_frame = std::make_shared<StopSendingFrame>();
        stop_frame->SetStreamID(_stream_id);
        stop_frame->SetAppErrorCode(error);

        _frame_list.emplace_back(stop_frame);
        ActiveToSend();
    }
}

void RecvStream::OnFrame(std::shared_ptr<IFrame> frame) {
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
            OnStreamFrame(frame);
            break;
        } else {
            LOG_ERROR("unexcept frame on recv stream. frame type:%d", frame_type);
        }
    }
}

IStream::TrySendResult RecvStream::TrySendData(IFrameVisitor* visitor) {
    IStream::TrySendData(nullptr);

    for (auto iter = _frame_list.begin(); iter != _frame_list.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = _frame_list.erase(iter);

        } else {
            return TSR_FAILED;
        }
    }
    return TSR_SUCCESS;
}

void RecvStream::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    if(!_recv_machine->OnFrame(frame->GetType())) {
        return;
    }
    
    auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
    if (stream_frame->IsFin()) {
        uint64_t fin_offset = stream_frame->GetOffset() + stream_frame->GetLength();
        if (_final_offset != 0 && fin_offset != _final_offset) {
            LOG_ERROR("invalid final size. size:%d", fin_offset);
            return;
        }
        _final_offset = fin_offset;
    }
    
    if (stream_frame->GetOffset() == _except_offset) {
        _recv_buffer->Write(stream_frame->GetData(), stream_frame->GetLength());
        _except_offset += stream_frame->GetLength();

        while (true) {
            auto iter = _out_order_frame.find(_except_offset);
            if (iter == _out_order_frame.end()) {
                break;
            }

            stream_frame = std::dynamic_pointer_cast<StreamFrame>(iter->second);
            _recv_buffer->Write(stream_frame->GetData(), stream_frame->GetLength());
            _except_offset += stream_frame->GetLength();
            _out_order_frame.erase(iter);
        }
        
        if (_final_offset != 0 && _final_offset == _except_offset && _out_order_frame.empty()) {
            _recv_machine->RecvAllData();
        }

        if (_recv_cb) {
            _recv_cb(_recv_buffer, 100); // TODO make error code. close
        }

        if (_recv_machine->CanAppReadAllData()) {
            _recv_machine->AppReadAllData();
        }

    } else {
        // recv a repeat packet
        if (_out_order_frame.find(stream_frame->GetOffset()) != _out_order_frame.end() ||
            stream_frame->GetOffset() < _except_offset) {
            return;
        }
        
        _out_order_frame[stream_frame->GetOffset()] = stream_frame;
    }

    // TODO put number to config
    if (_local_data_limit - _except_offset < 3096) {
        if (_recv_machine->CanSendMaxStrameDataFrame()) {
            _local_data_limit += 3096;
            auto max_frame = std::make_shared<MaxStreamDataFrame>();
            max_frame->SetStreamID(_stream_id);
            max_frame->SetMaximumData(_local_data_limit);
    
            ActiveToSend();
        }
    }
}

void RecvStream::OnStreamDataBlockFrame(std::shared_ptr<IFrame> frame) {
    if(!_recv_machine->OnFrame(frame->GetType())) {
        return;
    }
    
    auto block_frame = std::dynamic_pointer_cast<StreamDataBlockedFrame>(frame);
    LOG_WARN("peer send block. offset:%d", block_frame->GetMaximumData());

    auto max_frame = std::make_shared<MaxStreamDataFrame>();
    max_frame->SetStreamID(_stream_id);
    max_frame->SetMaximumData(_local_data_limit + 3096); // TODO. define increase steps
    _frame_list.emplace_back(max_frame);

    ActiveToSend();
}

void RecvStream::OnResetStreamFrame(std::shared_ptr<IFrame> frame) {
    if(!_recv_machine->OnFrame(frame->GetType())) {
        return;
    }
    
    auto reset_frame = std::dynamic_pointer_cast<ResetStreamFrame>(frame);
    uint64_t fin_offset = reset_frame->GetFinalSize();

    if (_final_offset != 0 && fin_offset != _final_offset) {
        LOG_ERROR("invalid final size. size:%d", fin_offset);
        return;
    }

    _final_offset = fin_offset;

    // TODO delay call it
    if (_recv_cb) {
        _recv_cb(_recv_buffer, reset_frame->GetAppErrorCode());
    }
}

}
