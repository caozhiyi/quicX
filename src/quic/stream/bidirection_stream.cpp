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

void BidirectionStream::Close(uint64_t error) {
    RecvStream::Close(error);
    SendStream::Close(error);
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
        if (StreamFrame::IsStreamFrame(frame_type)) {
            OnStreamFrame(frame);
            break;
        } else {
            LOG_ERROR("unexcept frame on recv stream. frame type:%d", frame_type);
        }
    }
}

IStream::TrySendResult BidirectionStream::TrySendData(IFrameVisitor* visitor) {
    return SendStream::TrySendData(visitor);
}

}