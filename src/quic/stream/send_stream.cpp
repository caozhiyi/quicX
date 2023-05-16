#include "common/log/log.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chains.h"
#include "common/alloter/alloter_interface.h"

#include "quic/stream/send_stream.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/send_state_machine.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/connection/connection_interface.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {

SendStream::SendStream(std::shared_ptr<BlockMemoryPool>& alloter, uint64_t id):
    ISendStream(id),
    _data_offset(0),
    _peer_data_limit(0) {
    _send_buffer = std::make_shared<BufferChains>(alloter);  
    _send_machine = std::make_shared<SendStreamStateMachine>();
}

SendStream::~SendStream() {

}

int32_t SendStream::Send(uint8_t* data, uint32_t len) {
    int32_t ret = _send_buffer->Write(data, len);
    if (_hope_send_cb) {
        _hope_send_cb(this);
    }
   return ret;
}

void SendStream::Close() {
    auto frame = std::make_shared<StreamFrame>();
    frame->SetFin();

    // check status
    if(!_send_machine->OnFrame(frame->GetType())) {
        return;
    }

    frame->SetStreamID(_stream_id);
    frame->SetOffset(_data_offset);

    _frame_list.emplace_back(frame);

    if (_hope_send_cb) {
        _hope_send_cb(this);
    }
}

void SendStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    switch (frame_type)
    {
    case FT_MAX_STREAM_DATA:
        OnMaxStreamDataFrame(frame);
        break;
    case FT_STOP_SENDING:
        OnStopSendingFrame(frame);
        break;
    default:
        LOG_ERROR("unexcept frame on send stream. frame type:%d", frame_type);
    }
}

bool SendStream::TrySendData(IFrameVisitor* visitor) {
    // TODO check stream state
    for (auto iter = _frame_list.begin(); iter != _frame_list.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = _frame_list.erase(iter);

        } else {
            return false;
        }
    }

    // make stream frame
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(_stream_id);

    // TODO not copy buffer
    uint8_t buf[1000] = {0};
    uint32_t size = _send_buffer->ReadNotMovePt(buf, 1000);
    frame->SetData(buf, size);

    if (!visitor->HandleFrame(frame)) {
        return false;
    }
    _send_buffer->MoveReadPt(size);
    return true;
}

void SendStream::Reset(uint64_t err) {
    // check status
    if(!_send_machine->OnFrame(FT_RESET_STREAM)) {
        return;
    }

    auto frame = std::make_shared<ResetStreamFrame>();
    frame->SetStreamID(_stream_id);
    frame->SetFinalSize(_data_offset);
    frame->SetAppErrorCode(err);

    _frame_list.emplace_back(frame);
    if (_hope_send_cb) {
        _hope_send_cb(this);
    }
}

void SendStream::OnMaxStreamDataFrame(std::shared_ptr<IFrame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxStreamDataFrame>(frame);
    uint64_t new_limit = max_data_frame->GetMaximumData();

    if (new_limit <= _peer_data_limit) {
        LOG_WARN("get a invalid max data stream. new limit:%d, current limit:%d", new_limit, _peer_data_limit);
        return;
    }

    uint32_t can_write_size = new_limit - _peer_data_limit;
    _peer_data_limit = new_limit;

    if (_send_cb) {
        _send_cb(can_write_size, 0);
    }
}

void SendStream::OnStopSendingFrame(std::shared_ptr<IFrame> frame) {
    auto stop_frame = std::dynamic_pointer_cast<StopSendingFrame>(frame);
    uint32_t err = stop_frame->GetAppErrorCode();

    Reset(0);

    if (_send_cb) {
        _send_cb(0, err);
    }
}

}
