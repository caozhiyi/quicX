#include <functional>
#include <unordered_map>

#include "common/buffer/multi_block_buffer_decode_wrapper.h"
#include "common/log/log.h"

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

static const std::unordered_map<uint16_t, std::function<std::shared_ptr<IFrame>()>> kFrameCreatorMap = {
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
    current_frame_type_(0) {}

FrameDecoder::~FrameDecoder() {}

bool FrameDecoder::DecodeFrames(std::shared_ptr<common::IBuffer> buffer, std::vector<std::shared_ptr<IFrame>>& frames) {
    // Return false if buffer is empty
    if (buffer->GetDataLength() == 0) {
        return false;
    }

    while (buffer->GetDataLength() > 0) {
        if (state_ == State::kReadingFrameType) {
            // Try to decode frame type
            common::MultiBlockBufferDecodeWrapper wrapper(buffer);
            uint16_t frame_type = 0;
            if (!wrapper.DecodeFixedUint16(frame_type)) {
                // DecodeFixedUint16 needs 2 bytes
                // If buffer has exactly 0 bytes, we already returned false at the start
                // If buffer has 1 byte, it's an error (incomplete frame type)
                // If buffer has 2+ bytes but decode failed, it's corrupt data
                common::LOG_ERROR(
                    "DecodeFrames: failed to decode frame type (corrupt or incomplete data), remaining=%u", wrapper.GetDataLength());
                return false;
            }

            // CRITICAL: Flush to commit the frame type read and advance buffer's read pointer
            // Without this, DataFrame::Decode will re-read these 2 bytes as length!
            wrapper.Flush();

            // Create frame instance
            auto creator = kFrameCreatorMap.find(frame_type);
            if (creator == kFrameCreatorMap.end()) {
                common::LOG_ERROR("FrameDecoder: unknown frame type=%u (0x%04x)", frame_type, frame_type);
                return false;
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
                common::LOG_ERROR("FrameDecoder: failed to decode frame type=%u", current_frame_type_);
                return false;
            }

            if (result == DecodeResult::kNeedMoreData) {
                // Frame is partially decoded, save state and wait for more data
                common::LOG_DEBUG("FrameDecoder: need more data to decode frame type=%u", current_frame_type_);
                return true;
            }

            // Frame successfully decoded
            uint32_t length_after = buffer->GetDataLength();
            uint32_t consumed = length_before - length_after;
            common::LOG_DEBUG("FrameDecoder: successfully decoded frame type=%u, consumed=%u bytes, remaining=%u",
                current_frame_type_, consumed, buffer->GetDataLength());

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
