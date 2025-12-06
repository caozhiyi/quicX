#ifndef QUIC_STREAM_STREAM_ID_GENERATOR
#define QUIC_STREAM_STREAM_ID_GENERATOR

#include <cstdint>

namespace quicx {
namespace quic {

class StreamIDGenerator {
public:
    enum StreamStarter {
        kClient = 0x0,
        kServer = 0x1,
    };

    enum StreamDirection {
        kBidirectional = 0x0,
        kUnidirectional = 0x2,
    };

    StreamIDGenerator(StreamStarter starter);
    ~StreamIDGenerator();

    uint64_t NextStreamID(StreamDirection direction);

    // Peek at the next stream ID without incrementing the counter
    // Used to check if stream creation would exceed limits before allocating
    uint64_t PeekNextStreamID(StreamDirection direction) const;

    static StreamDirection GetStreamDirection(uint64_t id);

private:
    StreamStarter starter_;
    uint64_t cur_bidirectional_id_;
    uint64_t cur_unidirectional_id_;
};

}  // namespace quic
}  // namespace quicx

#endif