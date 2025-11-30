#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"

#include "quic/connection/util.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/crypto_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/quicx/global_resource.h"
#include "quic/stream/fix_buffer_frame_visitor.h"

namespace quicx {
namespace quic {

FixBufferFrameVisitor::FixBufferFrameVisitor(uint32_t limit_size):
    encryption_level_(kApplication),
    cur_data_offset_(0),
    limit_data_offset_(0),
    frame_type_bit_(0),
    last_error_(FrameEncodeError::kNone) {
    auto chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        common::LOG_ERROR("failed to allocate buffer chunk");
        return;
    }
    chunk->SetLimitSize(limit_size);
    buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
}

FixBufferFrameVisitor::~FixBufferFrameVisitor() {}

bool FixBufferFrameVisitor::HandleFrame(std::shared_ptr<IFrame> frame) {
    // Reset error state before processing
    last_error_ = FrameEncodeError::kNone;

    // Update frame type bit for ACK-eliciting detection
    // Use GetFrameTypeBit() which handles STREAM frames correctly
    frame_type_bit_ |= frame->GetFrameTypeBit();

    if (frame->GetType() == FrameType::kCrypto) {
        auto crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(frame);
        encryption_level_ = crypto_frame->GetEncryptionLevel();
    }

    // Track STREAM frames for ACK tracking
    uint16_t ftype = frame->GetType();
    bool is_stream = StreamFrame::IsStreamFrame(ftype);

    if (is_stream) {
        auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
        if (stream_frame) {
            uint64_t stream_id = stream_frame->GetStreamID();
            uint64_t offset = stream_frame->GetOffset();
            uint32_t length = stream_frame->GetLength();
            bool has_fin = stream_frame->IsFin();

            // Update or insert stream data info (keep maximum offset per stream)
            auto it = stream_data_map_.find(stream_id);
            if (it == stream_data_map_.end()) {
                stream_data_map_[stream_id] = StreamDataInfo(stream_id, offset + length, has_fin);
            } else {
                // Update to maximum offset
                if (offset + length > it->second.max_offset) {
                    it->second.max_offset = offset + length;
                }
                // If any frame has FIN, mark it
                if (has_fin) {
                    it->second.has_fin = true;
                }
            }
        }
    }

    // Check buffer space before encoding
    uint32_t free_space = buffer_->GetFreeLength();
    uint16_t required_size = frame->EncodeSize();

    if (!frame->Encode(buffer_)) {
        // Encoding failed - determine the reason
        if (required_size > free_space) {
            last_error_ = FrameEncodeError::kInsufficientSpace;
            common::LOG_DEBUG("failed to encode frame due to insufficient space. type:%s, required:%u, available:%u",
                FrameType2String(frame->GetType()).c_str(), required_size, free_space);
        } else {
            last_error_ = FrameEncodeError::kOtherError;
            common::LOG_ERROR("failed to encode frame. type:%s", FrameType2String(frame->GetType()).c_str());
        }
        return false;
    }
    common::LOG_DEBUG(
        "encoded frame. type:%s, length:%u", FrameType2String(frame->GetType()).c_str(), buffer_->GetDataLength());
    return true;
}

std::vector<StreamDataInfo> FixBufferFrameVisitor::GetStreamDataInfo() const {
    std::vector<StreamDataInfo> result;
    result.reserve(stream_data_map_.size());
    for (const auto& pair : stream_data_map_) {
        result.push_back(pair.second);
    }
    return result;
}

}  // namespace quic
}  // namespace quicx