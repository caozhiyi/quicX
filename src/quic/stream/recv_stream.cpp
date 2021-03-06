#include "recv_stream.h"
#include "common/log/log.h"
#include "common/buffer/sort_buffer_queue.h"

#include "quic/frame/stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/connection/connection_interface.h"
#include "quic/frame/stream_data_blocked_frame.h"
namespace quicx {

RecvStreamStateMachine::RecvStreamStateMachine(StreamStatus s):
    StreamStateMachine(s) {

}

RecvStreamStateMachine::~RecvStreamStateMachine() {

}

bool RecvStreamStateMachine::OnFrame(uint16_t frame_type) {
    if (frame_type >= FT_STREAM && frame_type <= FT_STREAM_MAX) {
        if (frame_type & SFF_FIN) {
            if (_status == SS_RECV) {
                _status = SS_SIZE_KNOWN;
                return true;
            }
        }

        if (_status == SS_RECV || _status == SS_SIZE_KNOWN) {
            return true;
        }
        

    } else if (frame_type == FT_RESET_STREAM) {
        if (_status == SS_RECV || _status == SS_SIZE_KNOWN || _status == SS_DATA_RECVD) {
            _status = SS_RESET_RECVD;
            return true;
        }

    } else {
        LOG_ERROR("invalid frame type on recv stream. type:%d", frame_type);
        return false;
    }
    
    LOG_ERROR("current status not allow recv this frame. status:%d, frame type:%d", _status, frame_type);
    return false;
}

bool RecvStreamStateMachine::OnEvent(RecvStreamEvent event) {
    if (event == RSE_RECV_ALL_DATA) {
        if (_status == SS_SIZE_KNOWN) {
            _status = SS_DATA_RECVD;
        }
        
    } else if (event == RSE_READ_ALL_DATA) {
        if (_status == SS_DATA_RECVD) {
            _status = SS_DATA_READ;
        }

    } else {
        if (_status == SS_RESET_RECVD) {
            _status = SS_RESET_READ;
        }
    }
    return true;
}

RecvStream::RecvStream(StreamType type):
    Stream(type),
    _data_limit(0),
    _to_data_max(0),
    _final_offset(0) {
    _buffer = std::make_shared<SortBufferQueue>(_block_pool, _alloter);
    _state_machine = std::shared_ptr<RecvStreamStateMachine>();
}

RecvStream::~RecvStream() {

}

void RecvStream::Close() {
    auto stop_frame = std::make_shared<StopSendingFrame>();
    stop_frame->SetStreamID(_stream_id);
    stop_frame->SetAppErrorCode(0); // TODO

    _connection->Send(stop_frame);
}

void RecvStream::HandleFrame(std::shared_ptr<Frame> frame) {
    uint16_t frame_type = frame->GetType();
    if (frame_type == FT_STREAM) {
        HandleStreamFrame(frame);

    } else if (frame_type == FT_STREAM_DATA_BLOCKED) {
        HandleStreamDataBlockFrame(frame);

    } else if (frame_type == FT_RESET_STREAM) {
        HandleResetStreamFrame(frame);

    } else {
        LOG_ERROR("unexcept frame on send stream. frame type:%d", frame_type);
    }
}

void RecvStream::SetDataLimit(uint32_t limit) {
    _data_limit = limit;

    auto max_frame = std::make_shared<MaxStreamDataFrame>();
    max_frame->SetStreamID(_stream_id);
    max_frame->SetMaximumData(_buffer->GetDataOffset() + _data_limit);

    _connection->Send(max_frame);
}

void RecvStream::HandleStreamFrame(std::shared_ptr<Frame> frame) {
    if(!_state_machine->OnFrame(frame->GetType())) {
        return;
    }

    auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
    _buffer->Write(stream_frame->GetData(), stream_frame->GetOffset());

    if (_buffer->GetCanReadLength() > 0) {
        _read_back(_buffer, 0);
    }

    // send max stream data
    if (stream_frame->GetOffset() - _buffer->GetDataOffset() >= _to_data_max) {
        auto max_frame = std::make_shared<MaxStreamDataFrame>();
        max_frame->SetStreamID(_stream_id);
        max_frame->SetMaximumData(_buffer->GetDataOffset() + _data_limit);

        _connection->Send(max_frame);
    }
}

void RecvStream::HandleStreamDataBlockFrame(std::shared_ptr<Frame> frame) {
    if(!_state_machine->OnFrame(frame->GetType())) {
        return;
    }

    auto block_frame = std::dynamic_pointer_cast<StreamDataBlockedFrame>(frame);
    LOG_WARN("peer send block. offset:%d", block_frame->GetDataLimit());

    auto max_frame = std::make_shared<MaxStreamDataFrame>();
    max_frame->SetStreamID(_stream_id);
    max_frame->SetMaximumData(_buffer->GetDataOffset() + _data_limit);

    _connection->Send(max_frame);
}

void RecvStream::HandleResetStreamFrame(std::shared_ptr<Frame> frame) {
    if(!_state_machine->OnFrame(frame->GetType())) {
        return;
    }
    auto reset_frame = std::dynamic_pointer_cast<ResetStreamFrame>(frame);
    uint64_t fin_offset = reset_frame->GetFinalSize();

    if (_final_offset != 0 && fin_offset != _final_offset) {
        LOG_ERROR("invalid final size. size:%d", fin_offset);
        return;
    }

    _final_offset = fin_offset;

    _read_back(_buffer, reset_frame->GetAppErrorCode());
}

}
