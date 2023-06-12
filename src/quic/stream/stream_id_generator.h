#ifndef QUIC_STREAM_STREAM_ID_GENERATOR
#define QUIC_STREAM_STREAM_ID_GENERATOR

#include <cstdint>
#include "quic/stream/type.h"

namespace quicx {

class StreamIDGenerator {
public:
    enum StreamStarter {
        SS_CLIENT = 0x0,
        SS_SERVER = 0x1,
    };

    enum StreamDirection {
        SD_BIDIRECTIONAL  = 0x0,
        SD_UNIDIRECTIONAL = 0x2,
    };

    StreamIDGenerator(StreamStarter starter);
    ~StreamIDGenerator();

    uint64_t NextStreamID(StreamDirection direction);

    static StreamDirection GetStreamDirection(uint64_t id);

private:
    StreamStarter _starter;
    uint64_t _cur_bidirectional_id;
    uint64_t _cur_unidirectional_id;
};

}

#endif