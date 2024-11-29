#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/frame_interface.h"
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
    uint64_t id):
    SendStream(alloter, init_data_limit, id),
    RecvStream(alloter, init_data_limit, id) {

}

BidirectionStream::~BidirectionStream() {

}

void BidirectionStream::Reset(uint64_t error) {
    SendStream::Reset(error);
    RecvStream::Reset(error);
}

void BidirectionStream::Close() {
    RecvStream::Close();
    SendStream::Close();
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