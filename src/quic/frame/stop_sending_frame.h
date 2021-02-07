#ifndef QUIC_FRAME_STOP_SENDiNG_FRAME
#define QUIC_FRAME_STOP_SENDiNG_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class StopSendingFrame : public Frame {
public:
    StopSendingFrame() : Frame(FT_STOP_SENDING) {}
    ~StopSendingFrame() {}

private:
    uint64_t _stream_id;      // the Stream ID of the stream being ignored.
    uint32_t _app_error_code; // the application protocol error code.
};

}

#endif