#include "common/log/log.h"

#include "quic/frame/if_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/type.h"
#include "quic/stream/bidirection_stream.h"

namespace quicx {
namespace quic {

BidirectionStream::BidirectionStream(std::shared_ptr<common::IEventLoop> loop, uint64_t init_data_limit, uint64_t id,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    IStream(loop, id, active_send_cb, stream_close_cb, connection_close_cb),
    SendStream(loop, init_data_limit, id, active_send_cb, stream_close_cb, connection_close_cb),
    RecvStream(loop, init_data_limit, id, active_send_cb, stream_close_cb, connection_close_cb) {}

BidirectionStream::~BidirectionStream() {}

void BidirectionStream::Close() {
    if (!event_loop_->IsInLoopThread()) {
        event_loop_->RunInLoop([self = shared_from_this()]() {
            auto stream = std::dynamic_pointer_cast<BidirectionStream>(self);
            if (stream) {
                stream->Close();
            }
        });
        return;
    }

    SendStream::Close();
    // After closing send direction, check if both directions are terminal
    // Note: Close() sends FIN, but actual terminal state (Data Recvd) is reached
    // only after all data is ACKed. CheckStreamClose() will return early if not both terminal.
    CheckStreamClose();
}

void BidirectionStream::Reset(uint32_t error) {
    if (!event_loop_->IsInLoopThread()) {
        event_loop_->RunInLoop([self = shared_from_this(), error]() {
            auto stream = std::dynamic_pointer_cast<BidirectionStream>(self);
            if (stream) {
                stream->Reset(error);
            }
        });
        return;
    }

    // This is a public API for application to reset the stream
    // It should reset both directions when called with an actual error
    if (error != 0) {
        SendStream::Reset(error);  // send reset frame
        RecvStream::Reset(error);  // send stop sending frame
        // After resetting both directions, check if both are in terminal state
        CheckStreamClose();
    } else {
        // error == 0 is not a valid use of Reset() API (normal close should use Close())
        common::LOG_WARN("BidirectionStream::Reset called with error=0, use Close() instead. stream id:%d", stream_id_);
    }
}

int32_t BidirectionStream::Send(uint8_t* data, uint32_t len) {
    return SendStream::Send(data, len);
}

int32_t BidirectionStream::Send(std::shared_ptr<IBufferRead> buffer) {
    return SendStream::Send(buffer) > 0;
}

std::shared_ptr<IBufferWrite> BidirectionStream::GetSendBuffer() {
    return SendStream::GetSendBuffer();
}

bool BidirectionStream::Flush() {
    return SendStream::Flush();
}

void BidirectionStream::SetStreamWriteCallBack(stream_write_callback cb) {
    SendStream::SetStreamWriteCallBack(cb);
}

void BidirectionStream::SetStreamReadCallBack(stream_read_callback cb) {
    RecvStream::SetStreamReadCallBack(cb);
}

uint32_t BidirectionStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    uint32_t result = 0;

    switch (frame_type) {
        case FrameType::kStreamDataBlocked:
            OnStreamDataBlockFrame(frame);
            break;
        case FrameType::kResetStream:
            OnResetStreamFrame(frame);
            // After receiving RESET_STREAM, check if both directions are terminal
            CheckStreamClose();
            break;
        case FrameType::kMaxStreamData:
            OnMaxStreamDataFrame(frame);
            break;
        case FrameType::kStopSending:
            OnStopSendingFrame(frame);
            // After receiving STOP_SENDING (which triggers send-side reset),
            // check if both directions are terminal
            CheckStreamClose();
            break;
        default:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                result = OnStreamFrame(frame);
                // After receiving STREAM frames (possibly with FIN), check if both directions are terminal
                // This handles the case where recv side reaches Data Read state
                CheckStreamClose();
                return result;
            } else {
                common::LOG_ERROR("unexcept frame on recv stream. frame type:%d", frame_type);
            }
    }
    return result;
}

IStream::TrySendResult BidirectionStream::TrySendData(IFrameVisitor* visitor) {
    return SendStream::TrySendData(visitor);
}

void BidirectionStream::OnDataAcked(uint64_t max_offset, bool has_fin) {
    // Call parent's implementation
    SendStream::OnDataAcked(max_offset, has_fin);

    // After updating send-side ACK state, check if both directions are complete
    CheckStreamClose();
}

void BidirectionStream::CheckStreamClose() {
    // RFC 9000 Section 3.5: A bidirectional stream is closed when BOTH
    // sending and receiving parts reach their terminal states
    // Terminal states:
    // - Send: Data Recvd or Reset Recvd (all data ACKed)
    // - Recv: Data Read or Reset Read (app read all data/reset)
    bool send_terminal = (send_machine_->GetStatus() == StreamState::kDataRecvd ||
                          send_machine_->GetStatus() == StreamState::kResetRecvd);
    bool recv_terminal =
        (recv_machine_->GetStatus() == StreamState::kDataRead || recv_machine_->GetStatus() == StreamState::kResetRead);

    // Both directions must be in terminal state (AND not OR)
    if (send_terminal && recv_terminal) {
        if (stream_close_cb_) {
            common::LOG_DEBUG("bidirection stream fully closed. stream id:%d", stream_id_);
            stream_close_cb_(stream_id_);
        }
    }
}

}  // namespace quic
}  // namespace quicx