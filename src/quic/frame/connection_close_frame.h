#ifndef QUIC_FRAME_CONNECTION_CLOSE_FRAME
#define QUIC_FRAME_CONNECTION_CLOSE_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class ConnectionCloseFrame : public Frame {
public:
    ConnectionCloseFrame() : Frame(FT_CONNECTION_CLOSE) {}
    ~ConnectionCloseFrame() {}

private:
    uint32_t _error_code;    // indicates the reason for closing this connection.
    uint32_t _frame_type;    // the type of frame that triggered the error.
    uint32_t _reason_length; // the length of the reason phrase in bytes.
    char* _reason;           // why the connection was closed.
};

}

#endif