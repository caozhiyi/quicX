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

    bool result = manager.RetireIDBySequence(id2.GetSequenceNumber());
    EXPECT_TRUE(result);
    ASSERT_EQ(retired_sequences.size(), 2u);
    EXPECT_EQ(retired_sequences[0], id1.GetSequenceNumber());
    EXPECT_EQ(retired_sequences[1], id2.GetSequenceNumber());

    auto& current = manager.GetCurrentID();
    EXPECT_EQ(current.GetSequenceNumber(), id3.GetSequenceNumber());
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
