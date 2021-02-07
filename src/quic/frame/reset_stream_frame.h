#ifndef QUIC_FRAME_RESET_STREAM_FRAME
#define QUIC_FRAME_RESET_STREAM_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class ResetStreamFrame : public Frame {
public:
    ResetStreamFrame() : Frame(FT_RESET_STREAM) {}
    ~ResetStreamFrame() {}

private:
   uint64_t _stream_id;      // the Stream ID of the stream being terminated.
   uint32_t _app_error_code; // the application protocol error code.
   uint64_t _final_size;     // the final size of the stream by the RESET_STREAM sender.
}; 


}

#endif