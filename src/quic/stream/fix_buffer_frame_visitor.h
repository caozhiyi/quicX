#ifndef QUIC_STREAM_FIX_BUFFER_FRAME_VISITOR
#define QUIC_STREAM_FIX_BUFFER_FRAME_VISITOR

#include "quic/stream/frame_visitor_interface.h"

namespace quicx {
namespace quic {

class FixBufferFrameVisitor:
    public IFrameVisitor {
public:
    FixBufferFrameVisitor(uint32_t size);
    virtual ~FixBufferFrameVisitor();

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame);

    virtual std::shared_ptr<common::IBuffer> GetBuffer() { return _buffer; }

    virtual uint8_t GetEncryptionLevel() { return _encryption_level; }

    virtual void SetStreamDataSizeLimit(uint32_t size) { _left_stream_data_size = size; }

    virtual uint32_t GetLeftStreamDataSize() { return _left_stream_data_size - _cur_stream_data_size; }

    virtual void AddStreamDataSize(uint32_t size) { _cur_stream_data_size += size; }

    virtual uint64_t GetStreamDataSize() { return _cur_stream_data_size; }

private:
    uint8_t* _buffer_start;
    uint8_t _encryption_level;

    uint32_t _cur_stream_data_size;
    uint32_t _left_stream_data_size;
    std::shared_ptr<common::IBuffer> _buffer;
};


}
}
#endif