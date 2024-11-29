#ifndef QUIC_STREAM_FIX_BUFFER_FRAME_VISITOR
#define QUIC_STREAM_FIX_BUFFER_FRAME_VISITOR

#include "quic/stream/if_frame_visitor.h"

namespace quicx {
namespace quic {

/*
 fix buffer length visitor
*/
class FixBufferFrameVisitor:
    public IFrameVisitor {
public:
    FixBufferFrameVisitor(uint32_t size);
    virtual ~FixBufferFrameVisitor();

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame);

    virtual std::shared_ptr<common::IBuffer> GetBuffer() { return buffer_; }

    virtual uint8_t GetEncryptionLevel() { return encryption_level_; }

    virtual void SetStreamDataSizeLimit(uint32_t size) { limit_data_offset_ = size; }

    virtual uint32_t GetLeftStreamDataSize() { return limit_data_offset_ - cur_data_offset_; }

    virtual void AddStreamDataSize(uint32_t size) { cur_data_offset_ += size; }

    virtual uint64_t GetStreamDataSize() { return cur_data_offset_; }

private:
    uint8_t* cache_;
    uint8_t encryption_level_;

    uint32_t cur_data_offset_;
    uint32_t limit_data_offset_;
    std::shared_ptr<common::IBuffer> buffer_;
};


}
}
#endif