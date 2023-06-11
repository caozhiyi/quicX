#include <cstdlib> // for abort
#include "quic/stream/stream_id_generator.h"

namespace quicx {

StreamIDGenerator::StreamIDGenerator(StreamStarter starter):
    _starter(starter),
    _cur_bidirectional_id(0),
    _cur_unidirectional_id(0) {

}

StreamIDGenerator::~StreamIDGenerator() {

}

uint64_t StreamIDGenerator::NextStreamID(StreamDirection direction) {
    uint64_t next_stream = 0;
    StreamType stream_type;
    switch (direction)
    {
    case SD_BIDIRECTIONAL:
        next_stream = ++_cur_bidirectional_id;
        stream_type = _starter == SS_CLIENT ? ST_CLIENT_BIDIRECTIONAL : ST_SERVER_BIDIRECTIONAL;
        break;
    case SD_UNIIDIRECTIONAL:
        next_stream = ++_cur_unidirectional_id;
        stream_type = _starter == SS_CLIENT ? ST_CLIENT_UNIDIRECTIONAL : ST_SERVER_UNIDIRECTIONAL;
        break;
    default:
        abort();
    }

    return next_stream << 2 | stream_type;
}

StreamIDGenerator::StreamDirection StreamIDGenerator::GetStreamDirection(uint64_t id) {
    if (id & SD_UNIIDIRECTIONAL) {
        return SD_UNIIDIRECTIONAL;
    }
    return SD_BIDIRECTIONAL;
}

}
