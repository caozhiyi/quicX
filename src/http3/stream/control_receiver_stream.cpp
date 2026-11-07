#include "common/log/log.h"

#include "http3/frame/goaway_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/http/error.h"
#include "http3/stream/control_receiver_stream.h"

namespace quicx {
namespace http3 {

ControlReceiverStream::ControlReceiverStream(const std::shared_ptr<IQuicRecvStream>& stream,
    const std::shared_ptr<QpackEncoder>& /*qpack_encoder*/,
    const std::shared_ptr<QpackDecoder>& /*qpack_decoder*/,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(uint64_t id)>& goaway_handler,
    const std::function<void(const std::unordered_map<uint64_t, uint64_t>& settings)>& settings_handler):
    IRecvStream(StreamType::kControl, stream, error_handler),
    goaway_handler_(goaway_handler),
    settings_handler_(settings_handler) {
    stream_->SetStreamReadCallBack([this](auto a, auto b, auto c) { OnData(a, b, c); });
    // No send-side callback: the control stream is split into a sender side
    // (ControlSenderStream, owns IQuicSendStream) and a receiver side
    // (this class, owns IQuicRecvStream). Each side handles only its own
    // direction, so there is no send callback to register here.
}

ControlReceiverStream::~ControlReceiverStream() {
    // Note: Do NOT call stream_->Reset() here during destruction.
    // See QpackDecoderSenderStream destructor comment for details.
    // P1-H7a: unregister the read callback BEFORE dropping the stream
    // reference — the underlying QUIC stream outlives this wrapper (the
    // connection holds it), and a callback still capturing `this` would
    // fire into a destroyed object.
    if (stream_) {
        stream_->SetStreamReadCallBack(nullptr);
        stream_.reset();
    }
}

void ControlReceiverStream::OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
    if (error != 0) {
        // P1-H2a (RFC 9114 §6.2.1): the control stream was reset or its read
        // half errored — that is ALWAYS H3_CLOSED_CRITICAL_STREAM, a
        // connection error. Forwarding the raw transport error code would
        // leak a QUIC-level code into the H3 error space and DispatchH3Error
        // would reset instead of closing (control stream = critical).
        LOG_ERROR("ControlReceiverStream: control stream %llu errored (%u) — H3_CLOSED_CRITICAL_STREAM",
            (unsigned long long)stream_->GetStreamID(), error);
        error_handler_(stream_->GetStreamID(), static_cast<uint32_t>(Http3ErrorCode::kClosedCriticalStream));
        return;
    }
    size_t total_length = data->GetDataLength();
    LOG_DEBUG("ControlReceiverStream::OnData: data length=%zu, is_last=%d", total_length, is_last);

    // If buffer is empty (e.g., stream closed with FIN), nothing to do
    if (total_length == 0) {
        LOG_DEBUG("ControlReceiverStream::OnData: empty buffer, stream likely closed");
        return;
    }

    // Try parse HTTP/3 frames from unparsed data
    std::vector<std::shared_ptr<IFrame>> frames;
    size_t length_before = data->GetDataLength();
    LOG_DEBUG("ControlReceiverStream::OnData: before DecodeFrames, length_before=%zu", length_before);
    if (frame_decoder_.DecodeFrames(std::dynamic_pointer_cast<common::IBuffer>(data), frames)) {
        size_t length_after = data->GetDataLength();
        // DecodeFrames consumes frame data including the 2-byte frame type for each frame.
        // The consumed length is calculated from buffer length difference.
        size_t consumed = length_before - length_after;

        LOG_DEBUG(
            "ControlReceiverStream::OnData: DecodeFrames succeeded, decoded %zu frames, length_before=%zu, "
            "length_after=%zu, consumed=%zu bytes",
            frames.size(), length_before, length_after, consumed);
        for (const auto& frame : frames) {
            LOG_DEBUG("ControlReceiverStream::OnData: handling frame type=%d", frame->GetType());
            HandleFrame(frame);
        }

    } else {
        // RFC 9114 Section 7: Frame decoding failure on control stream is a connection error
        // Do NOT fall through to QPACK instruction handling with corrupted data
        LOG_ERROR(
            "ControlReceiverStream::OnData: DecodeFrames failed on control stream, "
            "closing connection with H3_FRAME_ERROR");
        error_handler_(stream_->GetStreamID(), Http3ErrorCode::kFrameError);
    }
}

void ControlReceiverStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    // RFC 9114 Section 6.2.1: The first frame on the control stream MUST be a SETTINGS frame
    if (!settings_received_ && frame->GetType() != FrameType::kSettings) {
        LOG_ERROR(
            "ControlReceiverStream: First frame on control stream must be SETTINGS, got type: %d", frame->GetType());
        error_handler_(stream_->GetStreamID(), Http3ErrorCode::kFrameUnexpected);
        return;
    }

    switch (frame->GetType()) {
        case FrameType::kGoAway: {
            auto goaway_frame = std::static_pointer_cast<GoAwayFrame>(frame);
            goaway_handler_(goaway_frame->GetStreamId());
            break;
        }
        case FrameType::kSettings: {
            // RFC 9114 Section 7.2.4: SETTINGS frame can only occur once
            if (settings_received_) {
                LOG_ERROR("ControlReceiverStream: Received duplicate SETTINGS frame");
                error_handler_(stream_->GetStreamID(), Http3ErrorCode::kSettingsError);
                return;
            }
            settings_received_ = true;
            auto settings_frame = std::static_pointer_cast<SettingsFrame>(frame);
            settings_handler_(settings_frame->GetSettings());
            break;
        }
        default:
            // P1-H2d (RFC 9114 §6.2.4): only SETTINGS / GOAWAY / CANCEL_PUSH /
            // MAX_PUSH_ID (and future unknown types, §9) are permitted on the
            // control stream. A DATA / HEADERS / PUSH_PROMISE arriving here
            // is H3_FRAME_UNEXPECTED — a connection error on a critical
            // stream (DispatchH3Error closes the connection). The old code
            // lumped these in with unknown types and silently ignored them.
            switch (frame->GetType()) {
                case FrameType::kData:
                case FrameType::kHeaders:
                case FrameType::kPushPromise:
                    LOG_ERROR("ControlReceiverStream: frame type %u not permitted on control stream, "
                              "H3_FRAME_UNEXPECTED",
                        frame->GetType());
                    error_handler_(stream_->GetStreamID(), static_cast<uint32_t>(Http3ErrorCode::kFrameUnexpected));
                    return;
                default:
                    // RFC 9114 Section 9: unknown frame types MUST be ignored
                    LOG_WARN("ControlReceiverStream: unknown frame type %d on control stream, ignoring",
                        frame->GetType());
                    break;
            }
            break;
    }
}

void ControlReceiverStream::HandleRawData(std::shared_ptr<IBufferRead> data) {
    // Interpret as QPACK encoder instructions and update dynamic table
    // P1-H4d: encoder-stream instructions drive the local DECODER table.
    qpack_decoder_->DecodeEncoderInstructions(std::dynamic_pointer_cast<common::IBuffer>(data));
}

}  // namespace http3
}  // namespace quicx
