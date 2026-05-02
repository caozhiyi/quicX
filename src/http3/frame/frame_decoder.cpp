#include <functional>
#include <unordered_map>

#include "common/buffer/buffer_decode_wrapper.h"
#include "common/log/log.h"
#include "common/qlog/qlog.h"

#include "http3/frame/cancel_push_frame.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decoder.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/frame/max_push_id_frame.h"
#include "http3/frame/push_promise_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/frame/type.h"

namespace quicx {
namespace http3 {

static const std::unordered_map<uint64_t, std::function<std::shared_ptr<IFrame>()>> kFrameCreatorMap = {
    {FrameType::kData, []() -> std::shared_ptr<IFrame> { return std::make_shared<DataFrame>(); }},
    {FrameType::kHeaders, []() -> std::shared_ptr<IFrame> { return std::make_shared<HeadersFrame>(); }},
    {FrameType::kCancelPush, []() -> std::shared_ptr<IFrame> { return std::make_shared<CancelPushFrame>(); }},
    {FrameType::kSettings, []() -> std::shared_ptr<IFrame> { return std::make_shared<SettingsFrame>(); }},
    {FrameType::kPushPromise, []() -> std::shared_ptr<IFrame> { return std::make_shared<PushPromiseFrame>(); }},
    {FrameType::kGoAway, []() -> std::shared_ptr<IFrame> { return std::make_shared<GoAwayFrame>(); }},
    {FrameType::kMaxPushId, []() -> std::shared_ptr<IFrame> { return std::make_shared<MaxPushIdFrame>(); }},
};

FrameDecoder::FrameDecoder():
    state_(State::kReadingFrameType),
    current_frame_(nullptr),
    current_frame_type_(0),
    skip_remaining_(0) {}

FrameDecoder::~FrameDecoder() {}

bool FrameDecoder::DecodeFrames(std::shared_ptr<common::IBuffer> buffer, std::vector<std::shared_ptr<IFrame>>& frames) {
    // Return false if buffer is empty
    if (buffer->GetDataLength() == 0) {
        return false;
    }

    while (buffer->GetDataLength() > 0) {
        // Handle partial skip of unknown frame payload from a previous call
        if (state_ == State::kSkippingUnknownFrame) {
            uint32_t available = buffer->GetDataLength();
            if (available >= skip_remaining_) {
                buffer->MoveReadPt(static_cast<uint32_t>(skip_remaining_));
                skip_remaining_ = 0;
                state_ = State::kReadingFrameType;
                continue;
            } else {
                skip_remaining_ -= available;
                buffer->MoveReadPt(available);
                return true;  // Need more data
            }
        }

        if (state_ == State::kReadingFrameType) {
            // Try to decode frame type using varint (RFC 9114 Section 9)
            common::BufferDecodeWrapper wrapper(buffer);
            uint64_t frame_type = 0;
            if (!wrapper.DecodeVarint(frame_type)) {
                // VarInt decode failed - either incomplete or corrupt data
                if (buffer->GetDataLength() == 0) {
                    return true;  // Need more data
                }
                common::LOG_ERROR(
                    "DecodeFrames: failed to decode frame type varint (corrupt or incomplete data), remaining=%u",
                    wrapper.GetDataLength());
                return false;
            }

            // CRITICAL: Flush to commit the frame type read and advance buffer's read pointer
            // Without this, DataFrame::Decode will re-read these bytes as length!
            wrapper.Flush();

            // Create frame instance
            auto creator = kFrameCreatorMap.find(frame_type);
            if (creator == kFrameCreatorMap.end()) {
                // RFC 9114 Section 9: Implementations MUST ignore unknown frame types.
                // Unknown frames follow the standard type-length-payload format,
                // so we read the length varint and skip that many bytes.
                common::BufferDecodeWrapper len_wrapper(buffer);
                uint64_t payload_length = 0;
                if (!len_wrapper.DecodeVarint(payload_length)) {
                    // Can't read length yet — need more data
                    common::LOG_DEBUG("FrameDecoder: unknown frame type=0x%llx, waiting for length",
                        (unsigned long long)frame_type);
                    return true;
                }
                len_wrapper.Flush();

                // Skip the payload bytes
                uint32_t available = buffer->GetDataLength();
                if (available < payload_length) {
                    skip_remaining_ = payload_length - available;
                    buffer->MoveReadPt(available);
                    state_ = State::kSkippingUnknownFrame;
                    common::LOG_DEBUG("FrameDecoder: skipping unknown frame type=0x%llx, need %llu more bytes",
                        (unsigned long long)frame_type, (unsigned long long)skip_remaining_);
                    return true;
                }

                buffer->MoveReadPt(static_cast<uint32_t>(payload_length));
                common::LOG_DEBUG("FrameDecoder: ignored unknown frame type=0x%llx, length=%llu",
                    (unsigned long long)frame_type, (unsigned long long)payload_length);
                continue;
            }

            current_frame_ = creator->second();
            current_frame_type_ = frame_type;
            state_ = State::kDecodingFrame;
        }

        if (state_ == State::kDecodingFrame) {
            // Try to decode the frame payload
            uint32_t length_before = buffer->GetDataLength();
            DecodeResult result = current_frame_->Decode(buffer, false);

            if (result == DecodeResult::kError) {
                common::LOG_ERROR("FrameDecoder: failed to decode frame type=0x%llx",
                    (unsigned long long)current_frame_type_);
                return false;
            }

            if (result == DecodeResult::kNeedMoreData) {
                // Frame is partially decoded, save state and wait for more data
                common::LOG_DEBUG("FrameDecoder: need more data to decode frame type=0x%llx",
                    (unsigned long long)current_frame_type_);
                return true;
            }

            // Frame successfully decoded
            uint32_t length_after = buffer->GetDataLength();
            uint32_t consumed = length_before - length_after;
            common::LOG_DEBUG("FrameDecoder: successfully decoded frame type=0x%llx, consumed=%u bytes, remaining=%u",
                (unsigned long long)current_frame_type_, consumed, buffer->GetDataLength());

            // Log http3:frame_parsed event
            if (qlog_trace_) {
                common::Http3FrameParsedData parsed_data;
                parsed_data.frame_type = static_cast<uint16_t>(current_frame_type_);
                parsed_data.stream_id = stream_id_;
                parsed_data.length = consumed;
                QLOG_HTTP3_FRAME_PARSED(qlog_trace_, parsed_data);
            }

            frames.push_back(current_frame_);

            // Reset state for next frame
            current_frame_ = nullptr;
            current_frame_type_ = 0;
            state_ = State::kReadingFrameType;
        }
    }

    return true;
}

}  // namespace http3
}  // namespace quicx
