#include <vector>

#include <gtest/gtest.h>

#include "quic/connection/connection_id_manager.h"

namespace quicx {
namespace quic {
namespace {

TEST(ConnectionIDManagerTest, GeneratorAddsIdsAndCallbacks) {
    std::vector<uint64_t> added_sequences;
    ConnectionIDManager manager(
        [&added_sequences](ConnectionID& id) { added_sequences.push_back(id.GetSequenceNumber()); });

    auto id1 = manager.Generator();
    auto id2 = manager.Generator();

    ASSERT_EQ(added_sequences.size(), 2u);
    EXPECT_EQ(added_sequences[0], id1.GetSequenceNumber());
    EXPECT_EQ(added_sequences[1], id2.GetSequenceNumber());

    // RFC 9000 §5.1.1: the very first connection ID issued by an endpoint MUST carry
    // sequence number 0 because that value is reserved for the SCID delivered in the
    // long header during the handshake. Subsequent CIDs (those carried in
    // NEW_CONNECTION_ID frames) increment from there.
    EXPECT_EQ(id1.GetSequenceNumber(), 0u);
    EXPECT_EQ(id2.GetSequenceNumber(), 1u);

    auto& current = manager.GetCurrentID();
    EXPECT_EQ(current.GetSequenceNumber(), id1.GetSequenceNumber());
    EXPECT_EQ(manager.GetAvailableIDCount(), 2u);
}

TEST(ConnectionIDManagerTest, RetireIdBySequenceInvokesCallback) {
    std::vector<uint64_t> retired_sequences;
    ConnectionIDManager manager(
        nullptr, [&retired_sequences](ConnectionID& id) { retired_sequences.push_back(id.GetSequenceNumber()); });

    auto id1 = manager.Generator();
    auto id2 = manager.Generator();
    auto id3 = manager.Generator();

    // RFC 9000 §19.16: RETIRE_CONNECTION_ID retires *exactly one* CID identified by
    // the sequence_number field. Earlier the implementation eagerly removed every
    // entry with sequence_number <= the requested one, but that corrupted the local
    // CID pool when the peer retired CIDs out of order. The test therefore now
    // asserts the single-retire semantics that match the documented behaviour in
    // ConnectionIDManager::RetireIDBySequence.
    bool result = manager.RetireIDBySequence(id2.GetSequenceNumber());
    EXPECT_TRUE(result);
    ASSERT_EQ(retired_sequences.size(), 1u);
    EXPECT_EQ(retired_sequences[0], id2.GetSequenceNumber());

    // id1 is still active (it was the first CID added so it became cur_id_), and
    // since id2 was not the active CID retiring it does not change the current.
    auto& current = manager.GetCurrentID();
    EXPECT_EQ(current.GetSequenceNumber(), id1.GetSequenceNumber());
    EXPECT_EQ(manager.GetAvailableIDCount(), 2u);

    // id3 is still in the pool and can be selected via UseNextID().
    (void)id3;
}

TEST(ConnectionIDManagerTest, UseNextIdAdvancesCurrent) {
    ConnectionIDManager manager;
    manager.Generator();  // id1: baseline, superseded by the later IDs below
    auto id2 = manager.Generator();
    auto id3 = manager.Generator();

    EXPECT_TRUE(manager.UseNextID());
    EXPECT_EQ(manager.GetCurrentID().GetSequenceNumber(), id2.GetSequenceNumber());

    EXPECT_TRUE(manager.UseNextID());
    EXPECT_EQ(manager.GetCurrentID().GetSequenceNumber(), id3.GetSequenceNumber());

    EXPECT_FALSE(manager.UseNextID());  // Only one ID left
}

// P0-S4: RETIRE_CONNECTION_ID generation driven by
// NEW_CONNECTION_ID.retire_prior_to must only cover sequence numbers that are
// still live in the pool. The retire storm vector: one datagram with ~40 NCID
// frames at retire_prior_to=256 used to enqueue min(256, cap) RETIRE frames
// per frame (~10k total) even though only a handful of sequences were ever
// issued. CollectSequencesBelow is the primitive the frame processor now
// uses; assert its contract directly.
TEST(ConnectionIDManagerTest, CollectSequencesBelowOnlyYieldsLiveSequences) {
    ConnectionIDManager manager;
    manager.Generator();  // seq 0
    manager.Generator();  // seq 1
    manager.Generator();  // seq 2
    manager.Generator();  // seq 3

    // Retire seq 1 out of order; seq 0 (the active one) and 2, 3 stay live.
    ASSERT_TRUE(manager.RetireIDBySequence(1));
    std::vector<uint64_t> live;
    manager.CollectSequencesBelow(256, live);
    ASSERT_EQ(live.size(), 3u);  // seq 0, 2, 3 — 1 is already gone
    EXPECT_EQ(live[0], 0u);
    EXPECT_EQ(live[1], 2u);
    EXPECT_EQ(live[2], 3u);

    // Batch-retire everything below 2 (only seq 0 still sits below it): the
    // pool keeps 2 and 3 and the manager promotes one to active.
    ASSERT_TRUE(manager.RetireIDsUpTo(2));
    live.clear();
    manager.CollectSequencesBelow(256, live);
    ASSERT_EQ(live.size(), 2u);
    EXPECT_EQ(live[0], 2u);
    EXPECT_EQ(live[1], 3u);

    // Drain: an active CID is never retired into an empty pool (the manager
    // needs one live CID), so issue seq 4 first and retire 2 and 3 below it.
    manager.Generator();  // seq 4
    ASSERT_TRUE(manager.RetireIDBySequence(2));
    ASSERT_TRUE(manager.RetireIDBySequence(3));
    live.clear();
    manager.CollectSequencesBelow(256, live);
    ASSERT_EQ(live.size(), 1u);  // only seq 4
    EXPECT_EQ(live[0], 4u);
}

// P0-S4: RFC 9000 §5.1.2 — a RETIRE_CONNECTION_ID for a sequence greater
// than any issued must be rejected as PROTOCOL_VIOLATION by the caller. The
// manager exposes the bound via GetLargestIssuedSequence.
TEST(ConnectionIDManagerTest, LargestIssuedSequenceBoundsRetireRange) {
    ConnectionIDManager manager;
    EXPECT_EQ(manager.GetLargestIssuedSequence(), 0u);  // nothing issued yet

    manager.Generator();  // seq 0
    manager.Generator();  // seq 1
    EXPECT_EQ(manager.GetLargestIssuedSequence(), 1u);

    manager.RetireIDBySequence(0);  // retiring does not lower the high-water mark
    EXPECT_EQ(manager.GetLargestIssuedSequence(), 1u);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
