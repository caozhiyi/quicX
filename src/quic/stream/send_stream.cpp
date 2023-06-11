#include "common/log/log.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chains.h"
#include "common/alloter/alloter_interface.h"

#include "quic/connection/error.h"
#include "quic/stream/send_stream.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/send_state_machine.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/connection/connection_interface.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {

SendStream::SendStream(std::shared_ptr<BlockMemoryPool>& alloter, uint64_t init_data_limit, uint64_t id):
    ISendStream(id),
    _to_fin(false),
    _data_offset(0),
    _peer_data_limit(init_data_limit) {
    _send_buffer = std::make_shared<BufferChains>(alloter);
}

SendStream::~SendStream() {

}

int32_t SendStream::Send(uint8_t* data, uint32_t len) {
    if (!_send_machine->CanSendAppData()) {
        return -1;
    }
    
    int32_t ret = _send_buffer->Write(data, len);
    if (_active_send_cb) {
        _active_send_cb(this);
    }
    return ret;
}

void SendStream::Reset(uint64_t error) {
    // check status
    if(!_send_machine->OnFrame(FT_RESET_STREAM)) {
        return;
    }

    auto frame = std::make_shared<ResetStreamFrame>();
    frame->SetStreamID(_stream_id);
    frame->SetFinalSize(_data_offset);
    frame->SetAppErrorCode(error);

    _frames_list.emplace_back(frame);
}

void SendStream::Close(uint64_t) {
    _to_fin = true;
    if (_active_send_cb) {
        _active_send_cb(this);
    }
}

uint32_t SendStream::OnFrame(std::shared_ptr<IFrame> frame) {
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
    return 0;
}

IStream::TrySendResult SendStream::TrySendData(IFrameVisitor* visitor) {
    IStream::TrySendData(nullptr);

    // check peer limit 
    // TODO put number to config
    if (_peer_data_limit - _data_offset < 2048) {
        if (_send_machine->CanSendDataBlockFrame()) {
            // make stream block frame
            std::shared_ptr<StreamDataBlockedFrame> frame = std::make_shared<StreamDataBlockedFrame>();
            frame->SetStreamID(_stream_id);
            frame->SetMaximumData(_peer_data_limit);
    
            if (!visitor->HandleFrame(frame)) {
                return TSR_FAILED;
            }
        }
    }

    if (_peer_data_limit <= _data_offset) {
        return TSR_FAILED;
    }
    
    for (auto iter = _frames_list.begin(); iter != _frames_list.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = _frames_list.erase(iter);

        } else {
            return TSR_FAILED;
        }
    }

    if (!_send_machine->CanSendStrameFrame()) {
        return TSR_SUCCESS;
    }

    // make stream frame
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(_stream_id);
    frame->SetOffset(_data_offset);
    if (_to_fin) {
        frame->SetFin();
    }
    
    uint32_t stream_send_size = _peer_data_limit - _data_offset;
    uint32_t conn_send_size = visitor->GetLeftStreamDataSize();
    uint32_t send_size = stream_send_size > conn_send_size ? conn_send_size : stream_send_size;
    send_size = send_size > 1000 ? 1000 : send_size;

    // TODO not copy buffer
    uint8_t buf[1000] = {0};
    uint32_t size = _send_buffer->ReadNotMovePt(buf, send_size);
    frame->SetData(buf, size);

    if (!visitor->HandleFrame(frame)) {
        return TSR_FAILED;
    }
    visitor->AddStreamDataSize(send_size);

    _send_buffer->MoveReadPt(size);
    _data_offset += size;

    if (_sended_cb) {
        _sended_cb(size, 0);
    }
    return TSR_SUCCESS;
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

    if (_send_buffer->GetDataLength() > 0) {
        ActiveToSend();
    }
}

void SendStream::OnStopSendingFrame(std::shared_ptr<IFrame> frame) {
    auto stop_frame = std::dynamic_pointer_cast<StopSendingFrame>(frame);
    uint32_t err = stop_frame->GetAppErrorCode();

    if (_send_machine->CanSendResetStreamFrame()) {
        Reset(err);
    }

    if (_sended_cb) {
        _sended_cb(0, err);
    }
}

}
