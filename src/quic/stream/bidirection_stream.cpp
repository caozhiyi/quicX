#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/frame_interface.h"
#include "common/buffer/buffer_chains.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/recv_state_machine.h"
#include "quic/stream/send_state_machine.h"
#include "quic/stream/bidirection_stream.h"

namespace quicx {

BidirectionStream::BidirectionStream(std::shared_ptr<BlockMemoryPool> alloter, uint64_t id):
    SendStream(alloter, id),
    RecvStream(alloter, id) {

}

BidirectionStream::~BidirectionStream() {

}

void BidirectionStream::Close() {
    auto stop_frame = std::make_shared<StopSendingFrame>();
    stop_frame->SetStreamID(_stream_id);
    stop_frame->SetAppErrorCode(0); // TODO. add some error code

    _frame_list.emplace_back(stop_frame);

    if (_hope_send_cb) {
        _hope_send_cb(this);
    }
}

void BidirectionStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    switch (frame_type)
    {
    case FT_STREAM_DATA_BLOCKED:
        OnStreamDataBlockFrame(frame);
        break;
    case FT_RESET_STREAM:
        OnResetStreamFrame(frame);
        break;
    case FT_MAX_STREAM_DATA:
        OnMaxStreamDataFrame(frame);
        break;
    case FT_STOP_SENDING:
        OnStopSendingFrame(frame);
        break;
    default:
        if (frame_type >= FT_STREAM && frame_type <= FT_STREAM_MAX) {
            OnStreamFrame(frame);
            break;
        } else {
            LOG_ERROR("unexcept frame on recv stream. frame type:%d", frame_type);
        }
    }
}

TrySendResult BidirectionStream::TrySendData(IFrameVisitor* visitor) {
    // TODO check stream state
    for (auto iter = _frame_list.begin(); iter != _frame_list.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = _frame_list.erase(iter);

        } else {
            return TSR_FAILED;
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
        return TSR_FAILED;
    }
    _send_buffer->MoveReadPt(size);
    return TSR_SUCCESS;
}

int32_t BidirectionStream::Send(uint8_t* data, uint32_t len) {
    return SendStream::Send(data, len);
}

void BidirectionStream::Reset(uint64_t err) {
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

}