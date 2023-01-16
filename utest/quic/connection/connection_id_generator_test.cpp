#include <gtest/gtest.h>
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace {

TEST(connnection_id_generator_utest, generator) {
    uint8_t cid1[8] = {0};
    uint8_t cid2[8] = {0};

    ConnectionIDGenerator::Instance().Generator(cid1, 8);
    ConnectionIDGenerator::Instance().Generator(cid2, 8);

    bool eq = true;
    for (size_t i = 0; i < 8; i++) {
        if (cid1[i] != cid2[i]) {
            eq = false;
        }
    }
    EXPECT_FALSE(eq);
}

TEST(connnection_id_generator_utest, hash) {
    uint8_t cid1[8] = {0};
    uint8_t cid2[8] = {0};

    ConnectionIDGenerator::Instance().Generator(cid1, 8);
    ConnectionIDGenerator::Instance().Generator(cid2, 8);

    uint64_t h1 = ConnectionIDGenerator::Instance().Hash(cid1, 8);
    uint64_t h2 = ConnectionIDGenerator::Instance().Hash(cid1, 8);

    EXPECT_EQ(h1, h2);
}

}
}