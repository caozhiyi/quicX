#include "common/log/log.h"
#include "quic/connection/util.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/crypto_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/quicx/global_resource.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "quic/stream/fix_buffer_frame_visitor.h"

namespace quicx {
namespace quic {

FixBufferFrameVisitor::FixBufferFrameVisitor(uint32_t limit_size):
    encryption_level_(kApplication),
    cur_data_offset_(0),
    limit_data_offset_(0),
    frame_type_bit_(0) {
    auto chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        common::LOG_ERROR("failed to allocate buffer chunk");
        return;
    }
    chunk->SetLimitSize(limit_size);
    buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
}

FixBufferFrameVisitor::~FixBufferFrameVisitor() {

}

bool FixBufferFrameVisitor::HandleFrame(std::shared_ptr<IFrame> frame) {
    // Update frame type bit for ACK-eliciting detection
    // Use GetFrameTypeBit() which handles STREAM frames correctly
    frame_type_bit_ |= frame->GetFrameTypeBit();
    
    if (frame->GetType() == FrameType::kCrypto) {
        auto crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(frame);
        encryption_level_ = crypto_frame->GetEncryptionLevel();
    }
    
    // Track STREAM frames for ACK tracking
    if (StreamFrame::IsStreamFrame(frame->GetType())) {
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
            
            common::LOG_DEBUG("Tracked stream frame: stream_id=%llu, offset=%llu, length=%u, has_fin=%d, max_offset=%llu",
                             stream_id, offset, length, has_fin, stream_data_map_[stream_id].max_offset);
        }
    }
    
    common::LOG_DEBUG("encode to packet. type:%s", FrameType2String(frame->GetType()).c_str());
    if (!frame->Encode(buffer_)) {
        common::LOG_ERROR("failed to encode frame. type:%s", FrameType2String(frame->GetType()).c_str());
        return false;
    }
    common::LOG_DEBUG("encoded frame. type:%s, length:%u", FrameType2String(frame->GetType()).c_str(), buffer_->GetDataLength());
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

}
}