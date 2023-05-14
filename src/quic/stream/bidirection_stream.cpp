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
    ISendStream(id), 
    _alloter(alloter) {
    _recv_machine = std::make_shared<RecvStreamStateMachine>();
    _send_machine = std::make_shared<SendStreamStateMachine>();
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

}

bool BidirectionStream::TrySendData(IDataVisitor* visitior) {
    // TODO check stream state
    for (auto iter = _frame_list.begin(); iter != _frame_list.end();) {
        if (visitior->HandleFrame(*iter)) {
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

    if (!visitior->HandleFrame(frame)) {
        return false;
    }
    _send_buffer->MoveReadPt(size);
    return true;
}

int32_t BidirectionStream::Send(uint8_t* data, uint32_t len) {
    if (!_send_buffer) {
        _send_buffer = std::make_shared<BufferChains>(_alloter);
    }
    int32_t ret = _send_buffer->Write(data, len);
    if (_hope_send_cb) {
        _hope_send_cb(this);
    }
    return ret;
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