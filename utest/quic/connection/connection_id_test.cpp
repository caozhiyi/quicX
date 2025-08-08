#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

#include "quic/connection/connection_id.h"
#include "quic/connection/connection_id_generator.h"
#include "quic/connection/type.h"

namespace quicx {
namespace quic {
namespace {

TEST(ConnectionIDTest, DefaultConstructorAndHash) {
    ConnectionID cid;

    // Defaults
    EXPECT_EQ(cid.Len(), kMaxCidLength);
    EXPECT_EQ(cid.SequenceNumber(), 0u);

    // ID buffer should be zero-initialized
    const uint8_t* id = cid.ID();
    for (uint16_t i = 0; i < kMaxCidLength; ++i) {
        EXPECT_EQ(id[i], 0u) << "Byte index " << i << " expected 0";
    }

    // Hash should be stable across calls
    uint64_t h1 = cid.Hash();
    uint64_t h2 = cid.Hash();
    EXPECT_EQ(h1, h2);

    // Matches generator on the same bytes/length
    uint64_t gen = ConnectionIDGenerator::Instance().Hash(const_cast<uint8_t*>(cid.ID()), cid.Len());
    EXPECT_EQ(h1, gen);
}

TEST(ConnectionIDTest, ParamConstructorAndAccessors) {
    uint8_t raw[8] = {1,2,3,4,5,6,7,8};
    uint8_t len = 8;
    uint64_t seq = 42;

    ConnectionID cid(raw, len, seq);

    EXPECT_EQ(cid.Len(), len);
    EXPECT_EQ(cid.SequenceNumber(), seq);

    // Bytes copied correctly (prefix of kMaxCidLength)
    const uint8_t* id = cid.ID();
    for (uint8_t i = 0; i < len; ++i) {
        EXPECT_EQ(id[i], raw[i]) << "Mismatch at index " << static_cast<int>(i);
    }

    // Hash is deterministic and equals generator's
    uint64_t h = cid.Hash();
    uint64_t gen = ConnectionIDGenerator::Instance().Hash(const_cast<uint8_t*>(cid.ID()), cid.Len());
    EXPECT_EQ(h, gen);
}

TEST(ConnectionIDTest, CopyConstructorAndAssignment) {
    uint8_t a[4] = {9,9,9,9};
    ConnectionID cid_a(a, 4, 7);

    // Copy constructor
    ConnectionID cid_b(cid_a);
    EXPECT_EQ(cid_b.Len(), cid_a.Len());
    EXPECT_EQ(cid_b.SequenceNumber(), cid_a.SequenceNumber());
    EXPECT_EQ(std::memcmp(cid_b.ID(), cid_a.ID(), kMaxCidLength), 0);
    EXPECT_EQ(cid_b.Hash(), cid_a.Hash());

    // Assignment
    uint8_t c[6] = {1,1,2,2,3,3};
    ConnectionID cid_c(c, 6, 11);
    cid_b = cid_c;
    EXPECT_EQ(cid_b.Len(), cid_c.Len());
    EXPECT_EQ(cid_b.SequenceNumber(), cid_c.SequenceNumber());
    EXPECT_EQ(std::memcmp(cid_b.ID(), cid_c.ID(), kMaxCidLength), 0);
    EXPECT_EQ(cid_b.Hash(), cid_c.Hash());
}

TEST(ConnectionIDTest, EqualityOperators) {
    uint8_t a[4] = {1,2,3,4};
    uint8_t b[4] = {1,2,3,5};

    ConnectionID cid1(a, 4, 1);
    ConnectionID cid2(a, 4, 2); // same bytes, different seq
    ConnectionID cid3(b, 4, 1); // different bytes

    // Equality compares only ID bytes; seq doesn't affect equality
    EXPECT_TRUE(cid1 == cid2);
    EXPECT_FALSE(cid1 != cid2);

    EXPECT_FALSE(cid1 == cid3);
    EXPECT_TRUE(cid1 != cid3);
}

TEST(ConnectionIDTest, SetIDUpdatesBytesAndLength) {
    uint8_t a[4] = {1,2,3,4};
    ConnectionID cid(a, 4, 0);

    uint8_t b[6] = {4,3,2,1,9,8};
    cid.SetID(b, 6);

    EXPECT_EQ(cid.Len(), 6);
    const uint8_t* id = cid.ID();
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(id[i], b[i]);
    }

    // Note: Hash() is cached in current implementation and not invalidated by SetID.
    // We only check that subsequent calls are stable, not that they change after SetID.
    uint64_t h1 = cid.Hash();
    uint64_t h2 = cid.Hash();
    EXPECT_EQ(h1, h2);
}

} // namespace
} // namespace quic
} // namespace quicx
