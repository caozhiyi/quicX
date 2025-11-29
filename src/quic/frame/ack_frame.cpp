#include "common/log/log.h"
#include "quic/frame/ack_frame.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

AckFrame::AckFrame():
    IFrame(FrameType::kAck),
    largest_acknowledged_(0),
    ack_delay_(0),
    first_ack_range_(0) {}

AckFrame::~AckFrame() {}

bool AckFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    // Compute base ACK frame size locally to avoid virtual dispatch issues
    uint32_t need_size = EncodeSize();

    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    // Per RFC 9000 ACK frame format:
    // Largest Acknowledged, ACK Delay, ACK Range Count, First ACK Range
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(largest_acknowledged_), "failed to encode largest acknowledged");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(ack_delay_), "failed to encode ack delay");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(static_cast<uint64_t>(ack_ranges_.size())), "failed to encode ack range count");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(first_ack_range_), "failed to encode first ack range");

    for (size_t i = 0; i < ack_ranges_.size(); i++) {
        CHECK_ENCODE_ERROR(wrapper.EncodeVarint(ack_ranges_[i].GetGap()), "failed to encode gap");
        CHECK_ENCODE_ERROR(wrapper.EncodeVarint(ack_ranges_[i].GetAckRangeLength()), "failed to encode ack range length");
    }
    return true;
}

bool AckFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    uint32_t buffer_len_before = buffer->GetDataLength();
    common::LOG_DEBUG("AckFrame::Decode START: buffer_len=%u, with_type=%d", buffer_len_before, with_type);

    {
        common::BufferDecodeWrapper wrapper(buffer);
        if (with_type) {
            CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
            if (frame_type_ != FrameType::kAck && frame_type_ != FrameType::kAckEcn) {
                common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
                return false;
            }
        }

        // Per RFC 9000 ACK frame format:
        // Largest Acknowledged, ACK Delay, ACK Range Count, First ACK Range
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(largest_acknowledged_), "failed to decode largest acknowledged");
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(ack_delay_), "failed to decode ack delay");
        uint64_t ack_range_count = 0;
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(ack_range_count), "failed to decode ack range count");
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(first_ack_range_), "failed to decode first ack range");

        uint64_t gap;
        uint64_t range;
        for (uint64_t i = 0; i < ack_range_count; i++) {
            CHECK_DECODE_ERROR(wrapper.DecodeVarint(gap), "failed to decode gap");
            CHECK_DECODE_ERROR(wrapper.DecodeVarint(range), "failed to decode range");
            ack_ranges_.emplace_back(AckRange(gap, range));
        }
    }  // wrapper destroyed here, Flush() called

    uint32_t buffer_len_after = buffer->GetDataLength();
    common::LOG_DEBUG("AckFrame::Decode END: buffer_len=%u, consumed=%u, ranges=%lu", buffer_len_after,
        buffer_len_before - buffer_len_after, ack_ranges_.size());
    return true;
}

uint32_t AckFrame::EncodeSize() {
    // frame type encoded as fixed uint16
    uint32_t size = sizeof(uint16_t);
    // Largest Acknowledged
    size += common::GetEncodeVarintLength(largest_acknowledged_);
    // ACK Delay
    size += common::GetEncodeVarintLength(ack_delay_);
    // ACK Range Count
    size += common::GetEncodeVarintLength(static_cast<uint64_t>(ack_ranges_.size()));
    // First ACK Range
    size += common::GetEncodeVarintLength(first_ack_range_);
    // Ranges: each has Gap and ACK Range Length
    for (auto& r : ack_ranges_) {
        size += common::GetEncodeVarintLength(r.GetGap());
        size += common::GetEncodeVarintLength(r.GetAckRangeLength());
    }
    return size;
}

AckFrame::AckFrame(FrameType ft):
    IFrame(static_cast<uint16_t>(ft)),
    largest_acknowledged_(0),
    ack_delay_(0),
    first_ack_range_(0) {}

AckEcnFrame::AckEcnFrame():
    AckFrame(static_cast<FrameType>(FrameType::kAckEcn)),
    ect_0_(0),
    ect_1_(0),
    ecn_ce_(0) {}

AckEcnFrame::~AckEcnFrame() {}

bool AckEcnFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    if (!AckFrame::Encode(buffer)) {
        return false;
    }

    // Calculate the actual size needed for ECN fields using varint encoding
    uint32_t ect0_size = common::GetEncodeVarintLength(ect_0_);
    uint32_t ect1_size = common::GetEncodeVarintLength(ect_1_);
    uint32_t ecn_ce_size = common::GetEncodeVarintLength(ecn_ce_);
    uint32_t total_ecn_size = ect0_size + ect1_size + ecn_ce_size;

    auto span = buffer->GetWritableSpan();
    auto remain_size = span.GetLength();
    if (total_ecn_size > remain_size) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, total_ecn_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(ect_0_), "failed to encode ect 0");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(ect_1_), "failed to encode ect 1");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(ecn_ce_), "failed to encode ecn ce");

    return true;
}

bool AckEcnFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    if (!AckFrame::Decode(buffer, with_type)) {
        return false;
    }

    common::BufferDecodeWrapper wrapper(buffer);
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(ect_0_), "failed to decode ect 0");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(ect_1_), "failed to decode ect 1");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(ecn_ce_), "failed to decode ecn ce");

    return true;
}

uint32_t AckEcnFrame::EncodeSize() {
    // Calculate the actual size needed for ECN fields using varint encoding
    uint32_t ect0_size = common::GetEncodeVarintLength(ect_0_);
    uint32_t ect1_size = common::GetEncodeVarintLength(ect_1_);
    uint32_t ecn_ce_size = common::GetEncodeVarintLength(ecn_ce_);
    return ect0_size + ect1_size + ecn_ce_size;
}

}  // namespace quic
}  // namespace quicx