#include <gtest/gtest.h>
#include "quic/stream/stream_id_generator.h"

namespace quicx {
namespace quic {
namespace {

TEST(stream_id_generator_utest, client) {
    StreamIDGenerator generator(StreamIDGenerator::StreamStarter::kClient);

    uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_EQ(stream_id, 0);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_EQ(stream_id, 4);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_EQ(stream_id, 8);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_EQ(stream_id, 12);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_EQ(stream_id, 2);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_EQ(stream_id, 6);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_EQ(stream_id, 10); 

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_EQ(stream_id, 14);
}

TEST(stream_id_generator_utest, server) {
    StreamIDGenerator generator(StreamIDGenerator::StreamStarter::kServer);

    uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_EQ(stream_id, 1);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_EQ(stream_id, 5);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_EQ(stream_id, 9);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_EQ(stream_id, 13);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_EQ(stream_id, 3);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_EQ(stream_id, 7);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_EQ(stream_id, 11);

    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_EQ(stream_id, 15); 
}

}
}
}