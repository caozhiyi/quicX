#include "quic/connection/controler/anti_amplification_controller.h"

#include "gtest/gtest.h"

namespace quicx {
namespace quic {

class AntiAmplificationControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        controller_ = std::make_unique<AntiAmplificationController>();
    }

    std::unique_ptr<AntiAmplificationController> controller_;
};

// ============================================================================
// Basic State Management Tests
// ============================================================================

// Test: Constructor initializes in validated state
TEST_F(AntiAmplificationControllerTest, ConstructorInitializesValidated) {
    EXPECT_FALSE(controller_->IsUnvalidated());
    EXPECT_EQ(controller_->GetBytesSent(), 0u);
    EXPECT_EQ(controller_->GetBytesReceived(), 0u);
    EXPECT_EQ(controller_->GetRemainingBudget(), UINT64_MAX);
}

// Test: EnterUnvalidatedState sets state correctly
TEST_F(AntiAmplificationControllerTest, EnterUnvalidatedStateSetsState) {
    controller_->EnterUnvalidatedState();

    EXPECT_TRUE(controller_->IsUnvalidated());
    EXPECT_EQ(controller_->GetBytesSent(), 0u);
    EXPECT_EQ(controller_->GetBytesReceived(), 400u);  // Default initial credit
    EXPECT_EQ(controller_->GetRemainingBudget(), 1200u);  // 400 * 3
}

// Test: EnterUnvalidatedState with custom initial credit
TEST_F(AntiAmplificationControllerTest, EnterUnvalidatedStateCustomCredit) {
    controller_->EnterUnvalidatedState(1000);

    EXPECT_TRUE(controller_->IsUnvalidated());
    EXPECT_EQ(controller_->GetBytesReceived(), 1000u);
    EXPECT_EQ(controller_->GetRemainingBudget(), 3000u);  // 1000 * 3
}

// Test: ExitUnvalidatedState clears restrictions
TEST_F(AntiAmplificationControllerTest, ExitUnvalidatedStateClearsRestrictions) {
    controller_->EnterUnvalidatedState();
    controller_->OnBytesReceived(1000);
    controller_->OnBytesSent(500);

    controller_->ExitUnvalidatedState();

    EXPECT_FALSE(controller_->IsUnvalidated());
    EXPECT_EQ(controller_->GetBytesSent(), 0u);
    EXPECT_EQ(controller_->GetBytesReceived(), 0u);
    EXPECT_EQ(controller_->GetRemainingBudget(), UINT64_MAX);
}

// Test: ExitUnvalidatedState when already validated is safe
TEST_F(AntiAmplificationControllerTest, ExitUnvalidatedStateWhenValidatedIsSafe) {
    EXPECT_FALSE(controller_->IsUnvalidated());
    controller_->ExitUnvalidatedState();  // Should not crash
    EXPECT_FALSE(controller_->IsUnvalidated());
}

// ============================================================================
// Byte Tracking Tests
// ============================================================================

// Test: OnBytesReceived tracks received bytes
TEST_F(AntiAmplificationControllerTest, OnBytesReceivedTracksBytes) {
    controller_->EnterUnvalidatedState(0);  // Start with no initial credit

    controller_->OnBytesReceived(100);
    EXPECT_EQ(controller_->GetBytesReceived(), 100u);
    EXPECT_EQ(controller_->GetRemainingBudget(), 300u);  // 100 * 3

    controller_->OnBytesReceived(200);
    EXPECT_EQ(controller_->GetBytesReceived(), 300u);
    EXPECT_EQ(controller_->GetRemainingBudget(), 900u);  // 300 * 3
}

// Test: OnBytesReceived when validated does nothing
TEST_F(AntiAmplificationControllerTest, OnBytesReceivedWhenValidatedDoesNothing) {
    EXPECT_FALSE(controller_->IsUnvalidated());

    controller_->OnBytesReceived(1000);
    EXPECT_EQ(controller_->GetBytesReceived(), 0u);
}

// Test: OnBytesSent tracks sent bytes
TEST_F(AntiAmplificationControllerTest, OnBytesSentTracksBytes) {
    controller_->EnterUnvalidatedState(1000);

    controller_->OnBytesSent(500);
    EXPECT_EQ(controller_->GetBytesSent(), 500u);
    EXPECT_EQ(controller_->GetRemainingBudget(), 2500u);  // 3000 - 500

    controller_->OnBytesSent(1000);
    EXPECT_EQ(controller_->GetBytesSent(), 1500u);
    EXPECT_EQ(controller_->GetRemainingBudget(), 1500u);  // 3000 - 1500
}

// Test: OnBytesSent when validated does nothing
TEST_F(AntiAmplificationControllerTest, OnBytesSentWhenValidatedDoesNothing) {
    EXPECT_FALSE(controller_->IsUnvalidated());

    controller_->OnBytesSent(1000);
    EXPECT_EQ(controller_->GetBytesSent(), 0u);
}

// ============================================================================
// Amplification Limit Tests
// ============================================================================

// Test: CanSend allows sending within 3x limit
TEST_F(AntiAmplificationControllerTest, CanSendAllowsWithinLimit) {
    controller_->EnterUnvalidatedState(1000);  // Max send: 3000

    EXPECT_TRUE(controller_->CanSend(1000));
    EXPECT_TRUE(controller_->CanSend(2000));
    EXPECT_TRUE(controller_->CanSend(3000));
}

// Test: CanSend blocks sending beyond 3x limit
TEST_F(AntiAmplificationControllerTest, CanSendBlocksBeyondLimit) {
    controller_->EnterUnvalidatedState(1000);  // Max send: 3000

    EXPECT_FALSE(controller_->CanSend(3001));
    EXPECT_FALSE(controller_->CanSend(5000));
}

// Test: CanSend with incremental sends
TEST_F(AntiAmplificationControllerTest, CanSendWithIncrementalSends) {
    controller_->EnterUnvalidatedState(1000);  // Max send: 3000

    // Send 1000 bytes
    EXPECT_TRUE(controller_->CanSend(1000));
    controller_->OnBytesSent(1000);
    EXPECT_EQ(controller_->GetRemainingBudget(), 2000u);

    // Send another 1500 bytes (total 2500)
    EXPECT_TRUE(controller_->CanSend(1500));
    controller_->OnBytesSent(1500);
    EXPECT_EQ(controller_->GetRemainingBudget(), 500u);

    // Try to send 600 bytes - should fail (would total 3100)
    EXPECT_FALSE(controller_->CanSend(600));

    // Send 500 bytes exactly hits limit
    EXPECT_TRUE(controller_->CanSend(500));
    controller_->OnBytesSent(500);
    EXPECT_EQ(controller_->GetRemainingBudget(), 0u);

    // Can't send anymore
    EXPECT_FALSE(controller_->CanSend(1));
}

// Test: CanSend always allows when validated
TEST_F(AntiAmplificationControllerTest, CanSendAlwaysAllowsWhenValidated) {
    EXPECT_FALSE(controller_->IsUnvalidated());

    EXPECT_TRUE(controller_->CanSend(1000000));
    EXPECT_TRUE(controller_->CanSend(UINT64_MAX));
}

// Test: Budget increases when more data received
TEST_F(AntiAmplificationControllerTest, BudgetIncreasesWithReceivedData) {
    controller_->EnterUnvalidatedState(1000);  // Max send: 3000

    // Send 2000 bytes
    controller_->OnBytesSent(2000);
    EXPECT_EQ(controller_->GetRemainingBudget(), 1000u);
    EXPECT_FALSE(controller_->CanSend(1500));

    // Receive 500 more bytes (total 1500, new limit: 4500)
    controller_->OnBytesReceived(500);
    EXPECT_EQ(controller_->GetRemainingBudget(), 2500u);  // 4500 - 2000
    EXPECT_TRUE(controller_->CanSend(1500));
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

// Test: Zero initial credit
TEST_F(AntiAmplificationControllerTest, ZeroInitialCredit) {
    controller_->EnterUnvalidatedState(0);

    EXPECT_EQ(controller_->GetBytesReceived(), 0u);
    EXPECT_EQ(controller_->GetRemainingBudget(), 0u);
    EXPECT_FALSE(controller_->CanSend(1));

    // Receive data to get budget
    controller_->OnBytesReceived(100);
    EXPECT_EQ(controller_->GetRemainingBudget(), 300u);
    EXPECT_TRUE(controller_->CanSend(300));
}

// Test: Exact limit boundary
TEST_F(AntiAmplificationControllerTest, ExactLimitBoundary) {
    controller_->EnterUnvalidatedState(1000);  // Max: 3000

    // Send exactly to the limit
    EXPECT_TRUE(controller_->CanSend(3000));
    controller_->OnBytesSent(3000);
    EXPECT_EQ(controller_->GetRemainingBudget(), 0u);

    // Can't send even 1 byte more
    EXPECT_FALSE(controller_->CanSend(1));
}

// Test: Multiple enter/exit cycles
TEST_F(AntiAmplificationControllerTest, MultipleEnterExitCycles) {
    // First cycle
    controller_->EnterUnvalidatedState(500);
    controller_->OnBytesSent(1000);
    EXPECT_TRUE(controller_->IsUnvalidated());
    EXPECT_EQ(controller_->GetBytesSent(), 1000u);

    controller_->ExitUnvalidatedState();
    EXPECT_FALSE(controller_->IsUnvalidated());
    EXPECT_EQ(controller_->GetBytesSent(), 0u);

    // Second cycle
    controller_->EnterUnvalidatedState(1000);
    EXPECT_TRUE(controller_->IsUnvalidated());
    EXPECT_EQ(controller_->GetBytesSent(), 0u);  // Reset
    EXPECT_EQ(controller_->GetBytesReceived(), 1000u);
}

// ============================================================================
// IsNearLimit Tests
// ============================================================================

// Test: IsNearLimit returns false when not near limit
TEST_F(AntiAmplificationControllerTest, IsNearLimitFalseWhenNotNear) {
    controller_->EnterUnvalidatedState(1000);  // Max: 3000

    // Send 1000 bytes (33% of limit)
    controller_->OnBytesSent(1000);
    EXPECT_FALSE(controller_->IsNearLimit());

    // Send to 2000 bytes (66% of limit)
    controller_->OnBytesSent(1000);
    EXPECT_FALSE(controller_->IsNearLimit());
}

// Test: IsNearLimit returns true when at 90% of limit
TEST_F(AntiAmplificationControllerTest, IsNearLimitTrueAt90Percent) {
    controller_->EnterUnvalidatedState(1000);  // Max: 3000

    // Send 2700 bytes (90% of 3000)
    controller_->OnBytesSent(2700);
    EXPECT_TRUE(controller_->IsNearLimit());

    // Send 2800 bytes (93%)
    controller_->OnBytesSent(100);
    EXPECT_TRUE(controller_->IsNearLimit());
}

// Test: IsNearLimit returns false when validated
TEST_F(AntiAmplificationControllerTest, IsNearLimitFalseWhenValidated) {
    EXPECT_FALSE(controller_->IsUnvalidated());
    EXPECT_FALSE(controller_->IsNearLimit());

    controller_->EnterUnvalidatedState(1000);
    controller_->OnBytesSent(2900);  // 96% of limit
    EXPECT_TRUE(controller_->IsNearLimit());

    controller_->ExitUnvalidatedState();
    EXPECT_FALSE(controller_->IsNearLimit());
}

// Test: IsNearLimit returns false with no data received
TEST_F(AntiAmplificationControllerTest, IsNearLimitFalseWithNoDataReceived) {
    controller_->EnterUnvalidatedState(0);
    EXPECT_FALSE(controller_->IsNearLimit());
}

// ============================================================================
// Reset Tests
// ============================================================================

// Test: Reset clears counters but maintains state
TEST_F(AntiAmplificationControllerTest, ResetClearsCountersMaintainsState) {
    controller_->EnterUnvalidatedState(1000);
    controller_->OnBytesSent(500);

    EXPECT_EQ(controller_->GetBytesSent(), 500u);
    EXPECT_EQ(controller_->GetBytesReceived(), 1000u);
    EXPECT_TRUE(controller_->IsUnvalidated());

    controller_->Reset();

    EXPECT_EQ(controller_->GetBytesSent(), 0u);
    EXPECT_EQ(controller_->GetBytesReceived(), 0u);
    EXPECT_TRUE(controller_->IsUnvalidated());  // State unchanged
    EXPECT_EQ(controller_->GetRemainingBudget(), 0u);
}

// Test: Reset when validated
TEST_F(AntiAmplificationControllerTest, ResetWhenValidated) {
    EXPECT_FALSE(controller_->IsUnvalidated());

    controller_->Reset();

    EXPECT_FALSE(controller_->IsUnvalidated());  // Still validated
    EXPECT_EQ(controller_->GetBytesSent(), 0u);
    EXPECT_EQ(controller_->GetBytesReceived(), 0u);
}

// ============================================================================
// Integration Scenarios
// ============================================================================

// Test: Typical server handshake scenario
TEST_F(AntiAmplificationControllerTest, TypicalServerHandshakeScenario) {
    // Server receives Initial packet from unknown client
    controller_->EnterUnvalidatedState();  // 400 bytes initial credit
    controller_->OnBytesReceived(1200);    // Receive Initial packet

    // Server can now send up to (400 + 1200) * 3 = 4800 bytes
    EXPECT_EQ(controller_->GetRemainingBudget(), 4800u);

    // Server sends Initial + Handshake packets (~3000 bytes)
    EXPECT_TRUE(controller_->CanSend(3000));
    controller_->OnBytesSent(3000);
    EXPECT_EQ(controller_->GetRemainingBudget(), 1800u);

    // Client sends Handshake packet (successfully decrypted - address validated!)
    controller_->ExitUnvalidatedState();

    // Server can now send unlimited data
    EXPECT_TRUE(controller_->CanSend(1000000));
}

// Test: Path validation scenario
TEST_F(AntiAmplificationControllerTest, PathValidationScenario) {
    // Start path validation to new address
    controller_->EnterUnvalidatedState(400);

    // Send PATH_CHALLENGE (allowed even with just initial credit)
    EXPECT_TRUE(controller_->CanSend(100));
    controller_->OnBytesSent(100);

    // Receive PATH_RESPONSE
    controller_->OnBytesReceived(100);

    // Path validated - exit unvalidated state
    controller_->ExitUnvalidatedState();

    EXPECT_FALSE(controller_->IsUnvalidated());
}

// Test: Retry packet scenario
TEST_F(AntiAmplificationControllerTest, RetryPacketScenario) {
    controller_->EnterUnvalidatedState();  // 400 initial credit
    controller_->OnBytesReceived(1000);    // Receive Initial

    // Send Initial + Handshake (~2500 bytes)
    controller_->OnBytesSent(2500);

    // Total budget: (400 + 1000) * 3 = 4200
    // Sent: 2500
    // Remaining: 1700
    EXPECT_EQ(controller_->GetRemainingBudget(), 1700u);

    // Client doesn't respond, approaching limit
    controller_->OnBytesSent(1200);  // Total sent: 3700

    // Budget: 4200, Sent: 3700, Near limit threshold: 4200 * 0.9 = 3780
    // Should NOT be near limit yet (3700 < 3780)
    EXPECT_FALSE(controller_->IsNearLimit());

    controller_->OnBytesSent(100);  // Total sent: 3800
    // Now 3800 >= 3780, near limit
    EXPECT_TRUE(controller_->IsNearLimit());

    // Server should consider sending Retry to validate address
    // (Retry logic handled by caller, not this controller)
}

}  // namespace quic
}  // namespace quicx
