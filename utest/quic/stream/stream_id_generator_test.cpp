#include <gtest/gtest.h>
#include "quic/stream/stream_id_generator.h"

namespace quicx {
namespace quic {
namespace {

TEST(stream_id_generator_utest, client) {
    StreamIDGenerator generator(StreamIDGenerator::SS_CLIENT);

    // 100
    uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL);
    EXPECT_EQ(stream_id, 4);

    // 1000
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL);
    EXPECT_EQ(stream_id, 8);

    // 1100
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL);
    EXPECT_EQ(stream_id, 12);

    // 10000
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL);
    EXPECT_EQ(stream_id, 16);

    // 110
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_UNIDIRECTIONAL);
    EXPECT_EQ(stream_id, 6);

    // 1010
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_UNIDIRECTIONAL);
    EXPECT_EQ(stream_id, 10);

    // 1110
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_UNIDIRECTIONAL);
    EXPECT_EQ(stream_id, 14); 

    // 10010
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_UNIDIRECTIONAL);
    EXPECT_EQ(stream_id, 18);
}

TEST(stream_id_generator_utest, server) {
    StreamIDGenerator generator(StreamIDGenerator::SS_SERVER);

    // 101
    uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL);
    EXPECT_EQ(stream_id, 5);

    // 1001
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL);
    EXPECT_EQ(stream_id, 9);

    // 1101
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL);
    EXPECT_EQ(stream_id, 13);

    // 10001
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL);
    EXPECT_EQ(stream_id, 17);

    // 111
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_UNIDIRECTIONAL);
    EXPECT_EQ(stream_id, 7);

    // 1011
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_UNIDIRECTIONAL);
    EXPECT_EQ(stream_id, 11);

    // 1111
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_UNIDIRECTIONAL);
    EXPECT_EQ(stream_id, 15);

    // 10011
    stream_id = generator.NextStreamID(StreamIDGenerator::SD_UNIDIRECTIONAL);
    EXPECT_EQ(stream_id, 19); 
}

}
}
}