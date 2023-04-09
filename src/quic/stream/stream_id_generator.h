#ifndef QUIC_STREAM_STREAM_ID_GENERATOR
#define QUIC_STREAM_STREAM_ID_GENERATOR

#include <cstdint>
#include "quic/stream/type.h"

namespace quicx {

class StreamIDGenerator {
public:
    enum StreamStarter {
        SS_CLIENT = 1,
        SS_SERVER = 2,
    };

    enum StreamDirection {
        SD_BIDIRECTIONAL   = 1,
        SD_UNIIDIRECTIONAL = 2,
    };

    StreamIDGenerator(StreamStarter starter);
    ~StreamIDGenerator();

    uint64_t NextStreamID(StreamDirection direction);

private:
    StreamStarter _starter;
    uint64_t _cur_bidirectional_id;
    uint64_t _cur_unidirectional_id;
};

}

#endif