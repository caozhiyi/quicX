#include "send_stream.h"

#include "common/log/log.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/alloter_interface.h"

#include "quic/frame/stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/send_state_machine.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/connection/connection_interface.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {

SendStream::SendStream(StreamType type):
    Stream(type),
    _data_offset(0),
    _peer_data_limit(0) {
    _state_machine = std::shared_ptr<SendStreamStateMachine>();
}

SendStream::~SendStream() {

}

int32_t SendStream::Write(std::shared_ptr<Buffer> buffer, uint32_t len) {
    if (_peer_data_limit == 0) {
        LOG_WARN("peer remote hasn't set data limit");
        return 0;
    }

    if (len == 0) {
        len = buffer->GetCanReadLength();
    }

    if (len == 0) {
        LOG_WARN("send buffer is empty");
        return 0;
    }
    
    bool send_block = false;
    // send stream frame
    if (_data_offset < _peer_data_limit) {
        uint64_t limit_send = _peer_data_limit - _data_offset;
        if (limit_send < len) {
            len = limit_send;

        } else {
            send_block = true;
        }

        // check status
        if(!_state_machine->OnFrame(FT_STREAM)) {
            return 0;
        }

        _data_offset += len;

        auto frame = std::make_shared<StreamFrame>();
        frame->SetStreamID(_stream_id);
        frame->SetOffset(_data_offset);
        frame->SetData(buffer, len);

        //_connection->Send(frame);

    } else {
        send_block = true;
    }
    
    // send stream data block frame
    if (send_block) {
        // check status
        if(!_state_machine->OnFrame(FT_STREAM_DATA_BLOCKED)) {
            return 0;
        }
        auto frame = std::make_shared<StreamDataBlockedFrame>();
        frame->SetStreamID(_stream_id);
        frame->SetMaximumData(_peer_data_limit);

        //_connection->Send(frame);
    }
    return len;
}

int32_t SendStream::Write(const std::string &data) {
    auto buffer = std::make_shared<BufferQueue>(_block_pool, _alloter);
    buffer->Write(data.c_str(), data.length());

    return Write(buffer);
}

int32_t SendStream::Write(char* data, uint32_t len) {
    auto buffer = std::make_shared<BufferQueue>(_block_pool, _alloter);
    buffer->Write(data, len);

    return Write(buffer);
}

void SendStream::Close() {
    auto frame = std::make_shared<StreamFrame>();
    frame->SetFin();

    // check status
    if(!_state_machine->OnFrame(frame->GetType())) {
        return;
    }

    frame->SetStreamID(_stream_id);
    frame->SetOffset(_data_offset);

    //_connection->Send(frame);
}

void SendStream::HandleFrame(std::shared_ptr<Frame> frame) {
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
    if(!_state_machine->OnFrame(FT_RESET_STREAM)) {
        return;
    }

    auto frame = std::make_shared<ResetStreamFrame>();
    frame->SetStreamID(_stream_id);
    frame->SetFinalSize(_data_offset);
    frame->SetAppErrorCode(err);

    //_connection->Send(frame);
}

void SendStream::HandleMaxStreamDataFrame(std::shared_ptr<Frame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxStreamDataFrame>(frame);
    uint64_t new_limit = max_data_frame->GetMaximumData();

    if (new_limit <= _peer_data_limit) {
        LOG_WARN("get a invalid max data stream. new limit:%d, current limit:%d", new_limit, _peer_data_limit);
        return;
    }

    uint32_t can_write_size = new_limit - _peer_data_limit;
    _peer_data_limit = new_limit;

    _write_back(can_write_size, 0);
}

void SendStream::HandleStopSendingFrame(std::shared_ptr<Frame> frame) {
    auto stop_frame = std::dynamic_pointer_cast<StopSendingFrame>(frame);
    uint32_t err = stop_frame->GetAppErrorCode();

    Reset(0);

    _write_back(0, err);
}

}
