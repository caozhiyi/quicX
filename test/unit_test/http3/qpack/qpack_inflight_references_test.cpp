// RFC 9204 2.1.1: "An encoder MUST NOT evict a dynamic table entry that has
// outstanding references" -- i.e. one that a header block it has sent still
// points to and the peer's decoder has neither acknowledged (Section
// Acknowledgment, 4.4.1) nor abandoned (Stream Cancellation, 4.4.2).
//
// Why it matters: eviction reaches the peer on the ENCODER stream, header
// blocks reach it on REQUEST streams, and nothing orders the two. A peer that
// applies our eviction before it decodes the block finds the entry gone ->
// QPACK_DECOMPRESSION_FAILED -> the whole connection is torn down.
//
// Also covers: the eviction guard looks at EVERY entry an insert would evict
// (not just the oldest), the block being encoded protects its own references,
// and the outstanding book is bounded.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/qpack/dynamic_table.h"
#include "http3/qpack/qpack_encoder.h"

namespace quicx {
namespace http3 {
namespace {

using FieldList = std::vector<std::pair<std::string, std::string>>;

std::shared_ptr<common::SingleBlockBuffer> Buf() {
    return std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(2048));
}

// Entry size = name + value + 32. "x-a"/"aaaa" -> 39 bytes. Capacity 100 holds
// two such entries; a third forces exactly one eviction.
const FieldList kA = {{"x-a", "aaaa"}};
const FieldList kB = {{"x-b", "bbbb"}};
const FieldList kC = {{"x-c", "cccc"}};

class InflightReferencesTest: public ::testing::Test {
protected:
    void SetUp() override {
        enc_.SetLocalMaxTableCapacity(100);
        enc_.SetPeerMaxTableCapacity(100);
        enc_.SetDynamicTableEnabled(true);
        enc_.SetInstructionSender([this](const FieldList& inserts) { inserted_ += inserts.size(); });
    }

    // abs 0 = x-a, abs 1 = x-b, both acknowledged (Known Received Count = 2).
    void FillTwoAcknowledgedEntries() {
        ASSERT_TRUE(enc_.Encode(kA, Buf()));
        ASSERT_TRUE(enc_.Encode(kB, Buf()));
        ASSERT_EQ(inserted_, 2u);
        enc_.OnPeerInsertCountIncrement(2);
        ASSERT_EQ(enc_.GetKnownReceivedCount(), 2u);
    }

    QpackEncoder enc_;
    size_t inserted_{0};
};

// ---- the headline property ------------------------------------------------

TEST_F(InflightReferencesTest, EntryReferencedByAnUnacknowledgedBlockIsNotEvicted) {
    FillTwoAcknowledgedEntries();

    // A block on stream 4 now references abs 0 (x-a).
    ASSERT_TRUE(enc_.Encode(kA, Buf(), /*stream_id=*/4));
    ASSERT_EQ(enc_.GetOutstandingSectionCount(), 1u);

    // Inserting x-c would evict abs 0 -- acknowledged as INSERTED, but the
    // block above still points at it. Must be refused (literal, no Insert).
    ASSERT_TRUE(enc_.Encode(kC, Buf(), /*stream_id=*/8));
    EXPECT_EQ(inserted_, 2u) << "an entry with an outstanding reference must not be evicted";

    // The peer acknowledges the block: the pin is released...
    EXPECT_TRUE(enc_.OnPeerSectionAck(4));
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), 0u);

    // ...and the same insert now goes through.
    ASSERT_TRUE(enc_.Encode(kC, Buf(), /*stream_id=*/8));
    EXPECT_EQ(inserted_, 3u);
}

TEST_F(InflightReferencesTest, StreamCancellationReleasesThePin) {
    FillTwoAcknowledgedEntries();
    ASSERT_TRUE(enc_.Encode(kA, Buf(), 4));
    ASSERT_TRUE(enc_.Encode(kA, Buf(), 4));  // two sections on the same stream
    ASSERT_EQ(enc_.GetOutstandingSectionCount(), 2u);

    ASSERT_TRUE(enc_.Encode(kC, Buf(), 8));
    EXPECT_EQ(inserted_, 2u);

    enc_.OnPeerStreamCancellation(4);  // peer abandoned the stream: neither will ever be acked
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), 0u);
    ASSERT_TRUE(enc_.Encode(kC, Buf(), 8));
    EXPECT_EQ(inserted_, 3u);
}

TEST_F(InflightReferencesTest, SectionAcksRetireTheEarliestSectionOfAStreamInOrder) {
    FillTwoAcknowledgedEntries();
    ASSERT_TRUE(enc_.Encode(kA, Buf(), 4));  // section 1 pins abs 0
    ASSERT_TRUE(enc_.Encode(kB, Buf(), 4));  // section 2 pins abs 1
    ASSERT_EQ(enc_.GetOutstandingSectionCount(), 2u);

    // Everything still pinned: no insert may evict.
    ASSERT_TRUE(enc_.Encode(kC, Buf(), 8));
    ASSERT_EQ(inserted_, 2u);

    // Acks are per stream and in order: the first retires section 1 (abs 0).
    ASSERT_TRUE(enc_.OnPeerSectionAck(4));
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), 1u);
    // Evicting abs 0 is now allowed (abs 1 stays pinned, and is not evicted).
    ASSERT_TRUE(enc_.Encode(kC, Buf(), 8));
    EXPECT_EQ(inserted_, 3u);

    ASSERT_TRUE(enc_.OnPeerSectionAck(4));
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), 0u);
}

TEST_F(InflightReferencesTest, SectionAckWithNothingOutstandingIsReportedAndIgnored) {
    EXPECT_FALSE(enc_.OnPeerSectionAck(4));
    enc_.OnPeerStreamCancellation(4);  // idempotent
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), 0u);
}

TEST_F(InflightReferencesTest, SectionAckDoesNotLowerKnownReceivedCount) {
    FillTwoAcknowledgedEntries();
    ASSERT_TRUE(enc_.Encode(kA, Buf(), 4));  // RIC 1 <= Known Received 2
    ASSERT_TRUE(enc_.OnPeerSectionAck(4));
    EXPECT_EQ(enc_.GetKnownReceivedCount(), 2u) << "an ack only ever raises the count";
}

// ---- the block protects its OWN references while it is being built --------

TEST_F(InflightReferencesTest, ABlocksEarlierReferenceSurvivesALaterInsertInTheSameBlock) {
    FillTwoAcknowledgedEntries();

    // One block: first header references abs 0, second would insert x-c and
    // evict abs 0. Pass 1 decides the two in order, so without protecting the
    // reference already chosen, the block would point at an evicted entry.
    FieldList both = {{"x-a", "aaaa"}, {"x-c", "cccc"}};
    ASSERT_TRUE(enc_.Encode(both, Buf(), 4));
    EXPECT_EQ(inserted_, 2u) << "the insert that would evict the block's own reference must be refused";
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), 1u);
}

// ---- the guard inspects every entry an insert would evict -----------------

TEST(DynamicTableEvictionGuardTest, MultiEntryEvictionChecksTheLastEvictedNotJustTheOldest) {
    DynamicTable t(120);  // three 39-byte entries fit (117)
    ASSERT_TRUE(t.AddHeaderItem("x-a", "aaaa"));  // abs 0
    ASSERT_TRUE(t.AddHeaderItem("x-b", "bbbb"));  // abs 1
    ASSERT_TRUE(t.AddHeaderItem("x-c", "cccc"));  // abs 2
    t.AdvanceKnownReceived(2);                    // abs 0,1 acknowledged; abs 2 not

    // 97 bytes: needs 117+97-120 = 94 freed -> evicts abs 0, 1 AND 2. The oldest
    // is acknowledged; the third is not. Checking only the oldest said "safe".
    EXPECT_TRUE(t.WouldEvictProtectedEntry(97));
    // 39 bytes: evicts just abs 0 (acknowledged) -> safe.
    EXPECT_FALSE(t.WouldEvictProtectedEntry(39));

    t.AdvanceKnownReceived(1);  // everything acknowledged
    EXPECT_FALSE(t.WouldEvictProtectedEntry(97));
}

TEST(DynamicTableEvictionGuardTest, PinFloorProtectsEverythingAtOrAboveIt) {
    DynamicTable t(120);
    ASSERT_TRUE(t.AddHeaderItem("x-a", "aaaa"));
    ASSERT_TRUE(t.AddHeaderItem("x-b", "bbbb"));
    ASSERT_TRUE(t.AddHeaderItem("x-c", "cccc"));
    t.AdvanceKnownReceived(3);  // all acknowledged: only the pin can protect now

    EXPECT_FALSE(t.WouldEvictProtectedEntry(39));  // evicts abs 0
    EXPECT_FALSE(t.WouldEvictProtectedEntry(97));

    t.SetEvictionPinFloor(1);  // some outstanding block references abs 1
    EXPECT_FALSE(t.WouldEvictProtectedEntry(39)) << "evicting abs 0 only: below the floor";
    EXPECT_TRUE(t.WouldEvictProtectedEntry(97)) << "would reach abs 1 and 2";

    t.SetEvictionPinFloor(UINT64_MAX);
    EXPECT_FALSE(t.WouldEvictProtectedEntry(97));
}

// ---- bounded memory --------------------------------------------------------

TEST_F(InflightReferencesTest, OutstandingBookIsBoundedAndDegradesToLiterals) {
    FillTwoAcknowledgedEntries();
    // A peer that never acknowledges: one section per stream, none ever retired.
    for (size_t i = 0; i < QpackEncoder::kMaxOutstandingSections; ++i) {
        ASSERT_TRUE(enc_.Encode(kA, Buf(), 4 * i));
    }
    ASSERT_EQ(enc_.GetOutstandingSectionCount(), QpackEncoder::kMaxOutstandingSections);

    // At the cap the block is still encoded (as a literal) and nothing grows.
    auto buf = Buf();
    ASSERT_TRUE(enc_.Encode(kA, buf, 4 * QpackEncoder::kMaxOutstandingSections));
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), QpackEncoder::kMaxOutstandingSections);
    EXPECT_GT(buf->GetDataLength(), 0u);

    // Acknowledgments catch up -> references resume.
    ASSERT_TRUE(enc_.OnPeerSectionAck(0));
    ASSERT_TRUE(enc_.Encode(kA, Buf(), 4 * QpackEncoder::kMaxOutstandingSections + 4));
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), QpackEncoder::kMaxOutstandingSections);
}

TEST_F(InflightReferencesTest, BlocksWithoutADynamicReferenceAreNeverTracked) {
    // RFC 9204 4.4.1: a block with Required Insert Count 0 is never acknowledged,
    // so tracking it would leak a section forever.
    FieldList static_only = {{":method", "GET"}};
    ASSERT_TRUE(enc_.Encode(static_only, Buf(), 4));
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), 0u);
}

TEST_F(InflightReferencesTest, WithoutAStreamIdNothingIsTracked) {
    FillTwoAcknowledgedEntries();
    ASSERT_TRUE(enc_.Encode(kA, Buf()));  // legacy / offline caller
    EXPECT_EQ(enc_.GetOutstandingSectionCount(), 0u);
}

}  // namespace
}  // namespace http3
}  // namespace quicx
