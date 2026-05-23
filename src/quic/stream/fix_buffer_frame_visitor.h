#ifndef QUIC_STREAM_FIX_BUFFER_FRAME_VISITOR
#define QUIC_STREAM_FIX_BUFFER_FRAME_VISITOR

#include <vector>
#include "quic/stream/if_frame_visitor.h"
#include "quic/connection/controler/send_control.h"

namespace quicx {
namespace quic {

/*
 fix buffer length visitor
*/
class FixBufferFrameVisitor:
    public IFrameVisitor {
public:
    FixBufferFrameVisitor(uint32_t limit_size);
    virtual ~FixBufferFrameVisitor();

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame) override;

    virtual std::shared_ptr<common::IBuffer> GetBuffer() override { return buffer_; }

    virtual uint8_t GetEncryptionLevel() override { return encryption_level_; }

    virtual void SetStreamDataSizeLimit(uint32_t size) override { limit_data_offset_ = size; }

    virtual uint32_t GetLeftStreamDataSize() override { return limit_data_offset_ - cur_data_offset_; }

    virtual void AddStreamDataSize(uint32_t size) override { cur_data_offset_ += size; }

    virtual uint64_t GetStreamDataSize() override { return cur_data_offset_; }
    
    virtual std::vector<StreamDataInfo> GetStreamDataInfo() const override;

    // Get accumulated frame type bit for all frames processed
    uint32_t GetFrameTypeBit() const { return frame_type_bit_; }

    // Get last encoding error
    virtual FrameEncodeError GetLastError() const override { return last_error_; }

private:
    uint8_t encryption_level_;

    uint32_t cur_data_offset_;
    uint32_t limit_data_offset_;
    std::shared_ptr<common::IBuffer> buffer_;

    // Track stream data for ACK tracking.
    // Each successfully encoded STREAM frame becomes one entry; we deliberately
    // do NOT merge per-stream entries because the ACK callback needs the
    // precise [offset_start, offset_start + length) range for selective
    // tracking — collapsing into a single max_offset reintroduces the
    // cumulative-ACK assumption that broke server-side stream completion.
    std::vector<StreamDataInfo> stream_data_list_;

    // Accumulated frame type bit for all frames processed
    uint32_t frame_type_bit_;

    // Last encoding error
    FrameEncodeError last_error_;
};


}
}
#endif