#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/frame/if_frame.h"
#include "quic/frame/stream_frame.h"
#include "common/buffer/buffer_chains.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/state_machine_recv.h"
#include "quic/stream/state_machine_send.h"
#include "quic/stream/bidirection_stream.h"

namespace quicx {
namespace quic {

BidirectionStream::BidirectionStream(std::shared_ptr<common::BlockMemoryPool> alloter,
    uint64_t init_data_limit, 
    uint64_t id,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    IStream(id, active_send_cb, stream_close_cb, connection_close_cb),
    SendStream(alloter, init_data_limit, id, active_send_cb, stream_close_cb, connection_close_cb),
    RecvStream(alloter, init_data_limit, id, active_send_cb, stream_close_cb, connection_close_cb) {

}

BidirectionStream::~BidirectionStream() {

}

void BidirectionStream::Close() {
    SendStream::Close();
}

void BidirectionStream::Reset(uint32_t error) {
    SendStream::Reset(error);
    RecvStream::Reset(error);
}

int32_t BidirectionStream::Send(uint8_t* data, uint32_t len) {
    return SendStream::Send(data, len);
}

int32_t BidirectionStream::Send(std::shared_ptr<common::IBufferRead> buffer) {
    return SendStream::Send(buffer);
}

void BidirectionStream::SetStreamWriteCallBack(stream_write_callback cb) {
    SendStream::SetStreamWriteCallBack(cb);
}

void BidirectionStream::SetStreamReadCallBack(stream_read_callback cb) {
    RecvStream::SetStreamReadCallBack(cb);
}

uint32_t BidirectionStream::OnFrame(std::shared_ptr<IFrame> frame) {
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
        if (StreamFrame::IsStreamFrame(frame_type)) {
            return OnStreamFrame(frame);
        } else {
            common::LOG_ERROR("unexcept frame on recv stream. frame type:%d", frame_type);
        }
    }
    return 0;
}

IStream::TrySendResult BidirectionStream::TrySendData(IFrameVisitor* visitor) {
    return SendStream::TrySendData(visitor);
}

}
}