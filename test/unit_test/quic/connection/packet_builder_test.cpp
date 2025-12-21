#include "quic/connection/packet_builder.h"

#include "gtest/gtest.h"

#include "common/buffer/single_block_buffer.h"
#include "quic/connection/connection_id_manager.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "quic/packet/header/long_header.h"
#include "quic/packet/header/short_header.h"
#include "quic/packet/init_packet.h"
#include "quic/stream/fix_buffer_frame_visitor.h"

namespace quicx {
namespace quic {

// Helper to create ConnectionIDManager with a specific CID
std::unique_ptr<ConnectionIDManager> CreateCIDManager(const ConnectionID& cid) {
    auto mgr = std::make_unique<ConnectionIDManager>(nullptr, nullptr);
    mgr->AddID(cid.GetID(), cid.GetLength());
    return mgr;
}

// Test fixture
class PacketBuilderTest : public ::testing::Test {
protected:
    void SetUp() override {
        builder_ = std::make_unique<PacketBuilder>();

        // Set up connection ID managers
        uint8_t local_cid_data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        uint8_t remote_cid_data[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
        local_cid_ = ConnectionID(local_cid_data, 8);
        remote_cid_ = ConnectionID(remote_cid_data, 8);

        local_cid_mgr_ = CreateCIDManager(local_cid_);
        remote_cid_mgr_ = CreateCIDManager(remote_cid_);

        // Set up frame visitor with some data
        frame_visitor_ = std::make_unique<FixBufferFrameVisitor>(1500);

        // Set up cryptographer (AES-128-GCM)
        cryptographer_ = std::make_shared<Aes128GcmCryptographer>();
    }

    std::unique_ptr<PacketBuilder> builder_;
    std::unique_ptr<ConnectionIDManager> local_cid_mgr_;
    std::unique_ptr<ConnectionIDManager> remote_cid_mgr_;
    std::unique_ptr<FixBufferFrameVisitor> frame_visitor_;
    std::shared_ptr<ICryptographer> cryptographer_;
    ConnectionID local_cid_;
    ConnectionID remote_cid_;
};

// Test: Build Initial packet successfully
TEST_F(PacketBuilderTest, BuildInitialPacket) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kInitial;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();
    ctx.add_padding = false;  // Disable padding for this test

    auto result = builder_->BuildPacket(ctx);

    EXPECT_TRUE(result.success);
    ASSERT_NE(result.packet, nullptr);
    EXPECT_EQ(result.packet->GetCryptoLevel(), kInitial);

    // Verify packet type
    auto init_packet = std::dynamic_pointer_cast<InitPacket>(result.packet);
    ASSERT_NE(init_packet, nullptr);

    // Verify header type is long header
    auto header = result.packet->GetHeader();
    EXPECT_EQ(header->GetHeaderType(), PacketHeaderType::kLongHeader);
}

// Test: Build Handshake packet successfully
TEST_F(PacketBuilderTest, BuildHandshakePacket) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kHandshake;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();

    auto result = builder_->BuildPacket(ctx);

    EXPECT_TRUE(result.success);
    ASSERT_NE(result.packet, nullptr);
    EXPECT_EQ(result.packet->GetCryptoLevel(), kHandshake);

    // Verify header type is long header
    auto header = result.packet->GetHeader();
    EXPECT_EQ(header->GetHeaderType(), PacketHeaderType::kLongHeader);
}

// Test: Build 0-RTT packet successfully
TEST_F(PacketBuilderTest, Build0RttPacket) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kEarlyData;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();

    auto result = builder_->BuildPacket(ctx);

    EXPECT_TRUE(result.success);
    ASSERT_NE(result.packet, nullptr);
    EXPECT_EQ(result.packet->GetCryptoLevel(), kEarlyData);

    // Verify header type is long header
    auto header = result.packet->GetHeader();
    EXPECT_EQ(header->GetHeaderType(), PacketHeaderType::kLongHeader);
}

// Test: Build 1-RTT packet successfully
TEST_F(PacketBuilderTest, Build1RttPacket) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kApplication;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();

    auto result = builder_->BuildPacket(ctx);

    EXPECT_TRUE(result.success);
    ASSERT_NE(result.packet, nullptr);
    EXPECT_EQ(result.packet->GetCryptoLevel(), kApplication);

    // Verify header type is short header (1-RTT uses short header)
    auto header = result.packet->GetHeader();
    EXPECT_EQ(header->GetHeaderType(), PacketHeaderType::kShortHeader);
}

// Test: Initial packet with token
TEST_F(PacketBuilderTest, InitialPacketWithToken) {
    uint8_t token_data[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                              0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kInitial;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();
    ctx.token_data = token_data;
    ctx.token_length = sizeof(token_data);
    ctx.add_padding = false;

    auto result = builder_->BuildPacket(ctx);

    EXPECT_TRUE(result.success);
    ASSERT_NE(result.packet, nullptr);

    auto init_packet = std::dynamic_pointer_cast<InitPacket>(result.packet);
    ASSERT_NE(init_packet, nullptr);
}

// Test: Initial packet with padding
TEST_F(PacketBuilderTest, InitialPacketWithPadding) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kInitial;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();
    ctx.add_padding = true;

    auto result = builder_->BuildPacket(ctx);

    EXPECT_TRUE(result.success);
    ASSERT_NE(result.packet, nullptr);

    // Verify padding was added (frame visitor buffer should have >= 1200 bytes)
    uint32_t buffer_size = frame_visitor_->GetBuffer()->GetDataLength();
    EXPECT_GE(buffer_size, 1100u);  // Allow some headroom
}

// Test: Packet with packet number set
TEST_F(PacketBuilderTest, PacketWithPacketNumber) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kApplication;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();
    ctx.packet_number = 12345;

    auto result = builder_->BuildPacket(ctx);

    EXPECT_TRUE(result.success);
    ASSERT_NE(result.packet, nullptr);
    EXPECT_EQ(result.packet->GetPacketNumber(), 12345u);

    // Verify packet number length is set
    auto header = result.packet->GetHeader();
    EXPECT_GT(header->GetPacketNumberLength(), 0u);
}

// Test: Error - null cryptographer
TEST_F(PacketBuilderTest, ErrorNullCryptographer) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kInitial;
    ctx.cryptographer = nullptr;  // Null
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();

    auto result = builder_->BuildPacket(ctx);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.packet, nullptr);
    EXPECT_FALSE(result.error_message.empty());
}

// Test: Error - null frame visitor
TEST_F(PacketBuilderTest, ErrorNullFrameVisitor) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kInitial;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = nullptr;  // Null
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();

    auto result = builder_->BuildPacket(ctx);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.packet, nullptr);
    EXPECT_FALSE(result.error_message.empty());
}

// Test: Error - null connection ID managers
TEST_F(PacketBuilderTest, ErrorNullConnectionIDManagers) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kInitial;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = nullptr;   // Null
    ctx.remote_cid_manager = nullptr;  // Null

    auto result = builder_->BuildPacket(ctx);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.packet, nullptr);
    EXPECT_FALSE(result.error_message.empty());
}

// Test: Connection IDs are correctly set
TEST_F(PacketBuilderTest, ConnectionIDsAreSet) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kHandshake;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();

    auto result = builder_->BuildPacket(ctx);

    EXPECT_TRUE(result.success);
    ASSERT_NE(result.packet, nullptr);

    auto header = result.packet->GetHeader();
    auto long_header = dynamic_cast<LongHeader*>(header);
    ASSERT_NE(long_header, nullptr);

    // Verify source CID
    EXPECT_EQ(long_header->GetSourceConnectionIdLength(), local_cid_.GetLength());
    EXPECT_EQ(memcmp(long_header->GetSourceConnectionId(), local_cid_.GetID(), local_cid_.GetLength()), 0);

    // Verify destination CID
    EXPECT_EQ(long_header->GetDestinationConnectionIdLength(), remote_cid_.GetLength());
    EXPECT_EQ(memcmp(long_header->GetDestinationConnectionId(), remote_cid_.GetID(), remote_cid_.GetLength()), 0);
}

// Test: Version is set for long headers
TEST_F(PacketBuilderTest, VersionIsSetForLongHeaders) {
    PacketBuilder::BuildContext ctx;
    ctx.encryption_level = kInitial;
    ctx.cryptographer = cryptographer_;
    ctx.frame_visitor = frame_visitor_.get();
    ctx.local_cid_manager = local_cid_mgr_.get();
    ctx.remote_cid_manager = remote_cid_mgr_.get();
    ctx.add_padding = false;

    auto result = builder_->BuildPacket(ctx);

    EXPECT_TRUE(result.success);
    ASSERT_NE(result.packet, nullptr);

    auto header = result.packet->GetHeader();
    auto long_header = dynamic_cast<LongHeader*>(header);
    ASSERT_NE(long_header, nullptr);

    // Verify version is set (should be kQuicVersions[0])
    EXPECT_NE(long_header->GetVersion(), 0u);
}

// Test: All encryption levels work
TEST_F(PacketBuilderTest, AllEncryptionLevelsWork) {
    EncryptionLevel levels[] = {kInitial, kHandshake, kEarlyData, kApplication};

    for (auto level : levels) {
        PacketBuilder::BuildContext ctx;
        ctx.encryption_level = level;
        ctx.cryptographer = cryptographer_;
        ctx.frame_visitor = frame_visitor_.get();
        ctx.local_cid_manager = local_cid_mgr_.get();
        ctx.remote_cid_manager = remote_cid_mgr_.get();
        ctx.add_padding = false;

        auto result = builder_->BuildPacket(ctx);

        EXPECT_TRUE(result.success) << "Failed for encryption level " << level;
        ASSERT_NE(result.packet, nullptr) << "Null packet for encryption level " << level;
        EXPECT_EQ(result.packet->GetCryptoLevel(), level);
    }
}

}  // namespace quic
}  // namespace quicx
