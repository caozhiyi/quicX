#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/goaway_frame.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/frame/settings_frame.h"
#include "common/buffer/buffer_read_view.h"
#include "http3/stream/control_receiver_stream.h"

namespace quicx {
namespace http3 {

ControlReceiverStream::ControlReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(uint64_t id)>& goaway_handler,
    const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler):
    IRecvStream(StreamType::kControl, stream, error_handler),
    goaway_handler_(goaway_handler),
    settings_handler_(settings_handler),
    parsed_offset_(0) {

    stream_->SetStreamReadCallBack(std::bind(&ControlReceiverStream::OnData, this, std::placeholders::_1, std::placeholders::_2));
    // TODO send callback
}

ControlReceiverStream::~ControlReceiverStream() {
    if (stream_) {
        stream_->Reset(0);
    }
}

void ControlReceiverStream::OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("IRecvStream::OnData error: %d", error);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }
    size_t total_length = data->GetDataLength();
    common::LOG_DEBUG("ControlReceiverStream::OnData: data length=%zu, parsed_offset_=%zu", total_length, parsed_offset_);

    // If we've already parsed all available data, nothing to do
    if (parsed_offset_ >= total_length) {
        common::LOG_DEBUG("ControlReceiverStream::OnData: all data already parsed");
        return;
    }

    // Create a view of the unparsed data (from parsed_offset_ onwards)
    size_t unparsed_length = total_length - parsed_offset_;
    auto unparsed_data = std::make_shared<common::BufferReadView>(
        const_cast<uint8_t*>(data->GetData()) + parsed_offset_,
        static_cast<uint32_t>(unparsed_length));

    // Debug: print first few bytes of unparsed data
    if (unparsed_length > 0) {
        uint8_t first_bytes[8] = {0};
        uint32_t print_len = unparsed_length > 8 ? 8 : unparsed_length;
        unparsed_data->ReadNotMovePt(first_bytes, print_len);
        common::LOG_DEBUG("ControlReceiverStream::OnData: unparsed data first %u bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                         print_len, first_bytes[0], first_bytes[1], first_bytes[2], first_bytes[3],
                         first_bytes[4], first_bytes[5], first_bytes[6], first_bytes[7]);
    }

    // Try parse HTTP/3 frames from unparsed data
    std::vector<std::shared_ptr<IFrame>> frames;
    size_t length_before = unparsed_data->GetDataLength();
    common::LOG_DEBUG("ControlReceiverStream::OnData: before DecodeFrames, length_before=%zu", length_before);
    if (DecodeFrames(unparsed_data, frames)) {
        size_t length_after = unparsed_data->GetDataLength();
        // DecodeFrames consumes frame data including the 2-byte frame type for each frame.
        // The consumed length is calculated from buffer length difference.
        size_t consumed = length_before - length_after;
        size_t old_parsed_offset = parsed_offset_;
        parsed_offset_ += consumed;
        
        common::LOG_DEBUG("ControlReceiverStream::OnData: DecodeFrames succeeded, decoded %zu frames, length_before=%zu, length_after=%zu, consumed=%zu bytes, parsed_offset_ %zu -> %zu", 
                         frames.size(), length_before, length_after, consumed, old_parsed_offset, parsed_offset_);
        for (const auto& frame : frames) {
            common::LOG_DEBUG("ControlReceiverStream::OnData: handling frame type=%d", frame->GetType());
            HandleFrame(frame);
        }
    } else {
        common::LOG_DEBUG("ControlReceiverStream::OnData: DecodeFrames failed, treating as raw QPACK instructions");
        HandleRawData(unparsed_data);
        // If we're treating as QPACK, mark all data as parsed to avoid re-processing
        parsed_offset_ = total_length;
    }
}

void ControlReceiverStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    switch (frame->GetType()) {
        case FrameType::kGoAway: {
            auto goaway_frame = std::static_pointer_cast<GoAwayFrame>(frame);
            goaway_handler_(goaway_frame->GetStreamId());
            break;
        }
        case FrameType::kSettings: {
            auto settings_frame = std::static_pointer_cast<SettingsFrame>(frame);
            settings_handler_(settings_frame->GetSettings());
            break;
        }
        default:
            common::LOG_ERROR("IStream::OnData unknown frame type: %d", frame->GetType());
            error_handler_(stream_->GetStreamID(), Http3ErrorCode::kFrameUnexpected);
            break;
    }
}

void ControlReceiverStream::HandleRawData(std::shared_ptr<common::IBufferRead> data) {
    // Interpret as QPACK encoder instructions and update dynamic table
    QpackEncoder enc;
    enc.DecodeEncoderInstructions(data);
}

}
}
