#ifndef QUIC_STREAM_FIX_BUFFER_FRAME_VISITOR
#define QUIC_STREAM_FIX_BUFFER_FRAME_VISITOR

#include "quic/stream/frame_visitor_interface.h"

namespace quicx {

class FixBufferFrameVisitor:
    public IFrameVisitor {
public:
    FixBufferFrameVisitor(uint32_t size);
    virtual ~FixBufferFrameVisitor();

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame);

    virtual uint32_t GetLeftSize();

    virtual std::shared_ptr<IBuffer> GetBuffer();

    virtual std::vector<FrameType>& GetFramesType();

    virtual uint8_t GetEncryptionLevel();

private:
    uint8_t* _buffer_start;
    uint8_t _encryption_level;
    std::shared_ptr<IBuffer> _buffer;
    std::vector<FrameType> _types;
};


}

#endif