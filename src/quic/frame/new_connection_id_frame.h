#ifndef QUIC_FRAME_NEW_CONNECTION_ID_FRAME
#define QUIC_FRAME_NEW_CONNECTION_ID_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class StreamsBlockedFrame : public Frame {
public:
    StreamsBlockedFrame() : Frame(FT_STREAMS_BLOCKED) {}
    ~StreamsBlockedFrame() {}

private:
    uint64_t _sequence_number;  // the connection ID by the sender.
    uint64_t _retire_prior_to;  // which connection IDs should be retired.
    uint32_t _length;           // the length of the connection ID.
    char* _connection_id;       // a connection ID of the specified length.
    char* _stateless_reset_token; // a 128-bit value that will be used for a stateless reset when the associated connection ID is used.
};

}

#endif