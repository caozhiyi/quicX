#include <cstdlib> // for abort
#include "quic/stream/stream_id_generator.h"

namespace quicx {
namespace quic {

StreamIDGenerator::StreamIDGenerator(StreamStarter starter):
    starter_(starter),
    cur_bidirectional_id_(0),
    cur_unidirectional_id_(0) {

}

StreamIDGenerator::~StreamIDGenerator() {

}

uint64_t StreamIDGenerator::NextStreamID(StreamDirection direction) {
    uint64_t next_stream = 0;
    switch (direction)
    {
    case StreamDirection::kBidirectional:
        next_stream = ++cur_bidirectional_id_;
        break;
    case StreamDirection::kUnidirectional:
        next_stream = ++cur_unidirectional_id_;
        break;
    default:
        abort();
    }

    return next_stream << 2 | (direction | starter_);
}

StreamIDGenerator::StreamDirection StreamIDGenerator::GetStreamDirection(uint64_t id) {
    if (id & StreamDirection::kUnidirectional) {
        return StreamDirection::kUnidirectional;
    }
    return StreamDirection::kBidirectional;
}

}
}
