#ifndef QUIC_FRAME_STREAM_FRAME
#define QUIC_FRAME_STREAM_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class StreamFrame : public Frame {
public:
    StreamFrame() : Frame(FT_STREAM) {}
    ~StreamFrame() {}

private:
    uint64_t _stream_id;  // indicating the stream ID of the stream.
    uint64_t _offset;     // the byte offset in the stream for the data in this STREAM frame.
    uint32_t _lentgh;     // the length of the Stream Data field in this STREAM frame.
    char* _data;          // the bytes from the designated stream to be delivered.
};

}

#endif