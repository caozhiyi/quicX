#include "send_stream.h"

#include "common/log/log.h"
#include "common/alloter/pool_block.h"
#include "common/alloter/alloter_interface.h"

#include "quic/frame/stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/send_state_machine.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/connection/connection_interface.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {

SendStream::SendStream(uint64_t id):
    ISendStream(id),
    _data_offset(0),
    _peer_data_limit(0) {
    _send_machine = std::shared_ptr<SendStreamStateMachine>();
}

SendStream::~SendStream() {

}

int32_t SendStream::Send(uint8_t* data, uint32_t len) {
   
   return 0;
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

    //_connection->Send(frame);
}

void SendStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    if (frame_type == FT_MAX_STREAM_DATA) {
        HandleMaxStreamDataFrame(frame);

    } else if (frame_type == FT_STOP_SENDING) {
        HandleStopSendingFrame(frame);

    } else {
        LOG_ERROR("unexcept frame on send stream. frame type:%d", frame_type);
    }
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

    //_connection->Send(frame);
}

void SendStream::HandleMaxStreamDataFrame(std::shared_ptr<IFrame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxStreamDataFrame>(frame);
    uint64_t new_limit = max_data_frame->GetMaximumData();

    if (new_limit <= _peer_data_limit) {
        LOG_WARN("get a invalid max data stream. new limit:%d, current limit:%d", new_limit, _peer_data_limit);
        return;
    }

    uint32_t can_write_size = new_limit - _peer_data_limit;
    _peer_data_limit = new_limit;

    //_write_back(can_write_size, 0);
}

void SendStream::HandleStopSendingFrame(std::shared_ptr<IFrame> frame) {
    auto stop_frame = std::dynamic_pointer_cast<StopSendingFrame>(frame);
    uint32_t err = stop_frame->GetAppErrorCode();

    Reset(0);

    //_write_back(0, err);
}

}
