#include <gtest/gtest.h>
#include <vector>

#include "quic/connection/connection_id_manager.h"

namespace quicx {
namespace quic {
namespace {

TEST(ConnectionIDManagerTest, GeneratorAddsIdsAndCallbacks) {
    std::vector<uint64_t> added_sequences;
    ConnectionIDManager manager([
        &added_sequences
    ](ConnectionID& id) {
        added_sequences.push_back(id.GetSequenceNumber());
    });

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
    ConnectionIDManager manager(nullptr, [
        &retired_sequences
    ](ConnectionID& id) {
        retired_sequences.push_back(id.GetSequenceNumber());
    });

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
    auto id1 = manager.Generator();
    auto id2 = manager.Generator();
    auto id3 = manager.Generator();

    EXPECT_TRUE(manager.UseNextID());
    EXPECT_EQ(manager.GetCurrentID().GetSequenceNumber(), id2.GetSequenceNumber());

    EXPECT_TRUE(manager.UseNextID());
    EXPECT_EQ(manager.GetCurrentID().GetSequenceNumber(), id3.GetSequenceNumber());

    EXPECT_FALSE(manager.UseNextID());  // Only one ID left
}

}  // namespace
}  // namespace quic
}  // namespace quicx
