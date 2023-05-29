#ifndef QUIC_FRAME_STREAM_FRAME_INTERFACE
#define QUIC_FRAME_STREAM_FRAME_INTERFACE

#include <memory>
#include "type.h"

#include "quic/frame/frame_interface.h"

namespace quicx {

class IStreamFrame:
    public IFrame {
public:
    IStreamFrame(uint16_t ft = FT_UNKNOW): IFrame(ft), _stream_id(0) {}
    virtual ~IStreamFrame() {}

    void SetStreamID(uint64_t id) { _stream_id = id; }
    uint64_t GetStreamID() { return _stream_id; }

protected:
    uint64_t _stream_id;     // indicating the stream ID of the stream.
};

}

#endif