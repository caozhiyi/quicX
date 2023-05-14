#ifndef QUIC_PROCESS_FIX_BUFFER_DATA_VISITOR
#define QUIC_PROCESS_FIX_BUFFER_DATA_VISITOR

#include "quic/process/data_visitor_interface.h"

namespace quicx {

class FixBufferDataVisitor:
    public IDataVisitor {
public:
    FixBufferDataVisitor(uint32_t size);
    virtual ~FixBufferDataVisitor();

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame);

    virtual uint32_t GetLeftSize();

    virtual std::shared_ptr<IBuffer> GetBuffer();

private:
    uint8_t* _buffer_start;
    std::shared_ptr<IBuffer> _buffer;
};


}

#endif