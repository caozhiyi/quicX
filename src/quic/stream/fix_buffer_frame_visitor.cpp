#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"

#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
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

    // Remember pre-encode size; if the frame fails to encode below, the
    // stream-data record we are about to push must be rolled back so that
    // unacked_packets_[ns][pn].stream_data does not advertise bytes that
    // never made it onto the wire.
    size_t pre_encode_stream_data_count = stream_data_list_.size();

    if (is_stream) {
        auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
        if (stream_frame) {
            uint64_t stream_id = stream_frame->GetStreamID();
            uint64_t offset = stream_frame->GetOffset();
            uint32_t length = stream_frame->GetLength();
            bool has_fin = stream_frame->IsFin();

            // One record per STREAM frame — never merge. The previous
            // implementation collapsed into a single (max_offset) entry per
            // stream and silently lost the offset_start/length, which made
            // SendStream::OnDataAcked treat selective ACKs as cumulative and
            // declared streams "Data Recvd" while early gaps were still
            // outstanding. See docs/release_plan_v0.1.0.md §2.B.3.
            stream_data_list_.emplace_back(stream_id, offset, length, has_fin);
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
        // Roll back any tentative stream-data record we appended above so the
        // unacked_packets bookkeeping never claims bytes that the wire didn't
        // actually carry.
        if (is_stream && stream_data_list_.size() > pre_encode_stream_data_count) {
            stream_data_list_.resize(pre_encode_stream_data_count);
        }
        return false;
    }
    common::LOG_DEBUG(
        "encoded frame. type:%s, length:%u", FrameType2String(frame->GetType()).c_str(), buffer_->GetDataLength());

    // Metrics: Frame transmitted
    common::Metrics::CounterInc(common::MetricsStd::FramesTxTotal);

    return true;
}

std::vector<StreamDataInfo> FixBufferFrameVisitor::GetStreamDataInfo() const {
    return stream_data_list_;
}

}  // namespace quic
}  // namespace quicx