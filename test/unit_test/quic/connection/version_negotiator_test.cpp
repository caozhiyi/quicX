// VersionNegotiator unit tests.
//
// These target the class directly rather than driving a full handshake, because
// the invariant that matters here is not observable end-to-end: the on-wire
// version is stored in three places (this layer, ConnectionCrypto, and the
// version_information transport parameter) and an end-to-end handshake can
// succeed with two of them wrong in the same way on both endpoints.
//
// Concretely, ApplyVersion()'s duty to push the version into ConnectionCrypto
// cannot be pinned through compatible_version_negotiation_test.cpp:
//   * on the upgrade path ConnectionCrypto::RekeyInitialForVersion() sets its
//     own copy anyway, masking an omission here;
//   * on the no-upgrade path no version change happens at all.
// Verified by mutation: deleting crypto_.SetVersion() from ApplyVersion() left
// every end-to-end test green. It fails these.

#include <memory>

#include <gtest/gtest.h>

#include "quic/common/constants.h"
#include "quic/connection/connection_crypto.h"
#include "quic/connection/error.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/version_negotiator.h"

namespace quicx {
namespace quic {
namespace {

class QuicVersionNegotiatorTest: public ::testing::Test {
protected:
    void SetUp() override {
        crypto_ = std::make_unique<ConnectionCrypto>();
        transport_param_ = std::make_unique<TransportParam>();
    }

    std::unique_ptr<VersionNegotiator> MakeNegotiator(bool is_server) {
        auto negotiator = std::make_unique<VersionNegotiator>(is_server, *crypto_, *transport_param_);
        negotiator->SetPushTransportParamCallback([this](TransportParam&) {
            ++push_count_;
            return true;
        });
        negotiator->SetCloseConnectionCallback([this](uint64_t error, uint16_t, std::string) {
            ++close_count_;
            last_close_error_ = error;
        });
        return negotiator;
    }

    std::unique_ptr<ConnectionCrypto> crypto_;
    std::unique_ptr<TransportParam> transport_param_;
    int push_count_{0};
    int close_count_{0};
    uint64_t last_close_error_{0};
};

// ===== The three-way consistency invariant =====

TEST_F(QuicVersionNegotiatorTest, DefaultsToV2OnBothLayers) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    EXPECT_EQ(negotiator->GetVersion(), kQuicVersion2);
    EXPECT_EQ(crypto_->GetVersion(), kQuicVersion2);
}

// This is the assertion that the end-to-end tests cannot make.
TEST_F(QuicVersionNegotiatorTest, ApplyVersionPushesIntoCrypto) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);

    negotiator->ApplyVersion(kQuicVersion1);

    EXPECT_EQ(negotiator->GetVersion(), kQuicVersion1);
    EXPECT_EQ(crypto_->GetVersion(), kQuicVersion1) << "ConnectionCrypto is the copy the send path reads; if it lags, "
                                                       "packets carry the wrong version while this layer looks correct";
}

// Third copy: the version_information TP must be re-encoded and re-pushed to
// TLS, or the peer rejects the connection under RFC 9368 §4.
TEST_F(QuicVersionNegotiatorTest, ApplyVersionRepushesTransportParamWhenPresent) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    negotiator->BuildLocalVersionInformation(*transport_param_);
    ASSERT_TRUE(transport_param_->HasVersionInformation());
    push_count_ = 0;

    negotiator->ApplyVersion(kQuicVersion1);

    EXPECT_EQ(push_count_, 1);
    EXPECT_EQ(transport_param_->GetChosenVersion(), kQuicVersion1);
}

// Before version_information exists there is nothing to re-encode, but the
// other two copies must still move.
TEST_F(QuicVersionNegotiatorTest, ApplyVersionStillSyncsWhenNoVersionInformationYet) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    ASSERT_FALSE(transport_param_->HasVersionInformation());

    negotiator->ApplyVersion(kQuicVersion1);

    EXPECT_EQ(negotiator->GetVersion(), kQuicVersion1);
    EXPECT_EQ(crypto_->GetVersion(), kQuicVersion1);
    EXPECT_EQ(push_count_, 0);
}

// ===== version_information construction (RFC 9368 §3) =====

TEST_F(QuicVersionNegotiatorTest, AdvertisesOnlyCurrentVersionWithoutPreference) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);

    negotiator->BuildLocalVersionInformation(*transport_param_);

    EXPECT_EQ(transport_param_->GetChosenVersion(), kQuicVersion2);
    ASSERT_EQ(transport_param_->GetAvailableVersions().size(), 1u);
    EXPECT_EQ(transport_param_->GetAvailableVersions()[0], kQuicVersion2);
}

TEST_F(QuicVersionNegotiatorTest, AdvertisesPreferenceAheadOfCurrentVersion) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    negotiator->ApplyVersion(kQuicVersion1);
    negotiator->SetPreferredVersion(kQuicVersion2);

    negotiator->BuildLocalVersionInformation(*transport_param_);

    EXPECT_EQ(transport_param_->GetChosenVersion(), kQuicVersion1);
    ASSERT_EQ(transport_param_->GetAvailableVersions().size(), 2u);
    EXPECT_EQ(transport_param_->GetAvailableVersions()[0], kQuicVersion2) << "preference comes first";
    EXPECT_EQ(transport_param_->GetAvailableVersions()[1], kQuicVersion1);
}

// A preference equal to the current version must not produce a duplicate entry.
TEST_F(QuicVersionNegotiatorTest, PreferenceEqualToCurrentIsNotDuplicated) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    negotiator->SetPreferredVersion(kQuicVersion2);

    negotiator->BuildLocalVersionInformation(*transport_param_);

    ASSERT_EQ(transport_param_->GetAvailableVersions().size(), 1u);
    EXPECT_EQ(transport_param_->GetAvailableVersions()[0], kQuicVersion2);
}

// ===== Compatible upgrade (RFC 9368 §4) =====

// Both roles share one implementation now; the DCID source is resolved by role
// rather than passed in, so a caller cannot supply the wrong one. With no DCID
// available the outcome is reported instead of decided, because the two callers
// disagree on severity.
TEST_F(QuicVersionNegotiatorTest, CompatibleUpgradeReportsMissingDcidRatherThanGuessing) {
    auto negotiator = MakeNegotiator(/*is_server=*/true);
    ASSERT_TRUE(transport_param_->GetOriginalDestinationConnectionId().empty());

    EXPECT_EQ(negotiator->ApplyCompatibleUpgrade(kQuicVersion1), VersionNegotiator::UpgradeResult::kNoDcid);

    // Nothing moved: a failed upgrade must not leave the copies split.
    EXPECT_EQ(negotiator->GetVersion(), kQuicVersion2);
    EXPECT_EQ(crypto_->GetVersion(), kQuicVersion2);
    EXPECT_EQ(close_count_, 0) << "severity is the caller's call";
}

TEST_F(QuicVersionNegotiatorTest, ClientUpgradeWithoutCachedDcidReportsNoDcid) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    ASSERT_TRUE(crypto_->GetInitialSecretDcid().empty());

    EXPECT_EQ(negotiator->ApplyCompatibleUpgrade(kQuicVersion1), VersionNegotiator::UpgradeResult::kNoDcid);
    EXPECT_EQ(negotiator->GetVersion(), kQuicVersion2);
    EXPECT_EQ(crypto_->GetVersion(), kQuicVersion2);
}

// ===== Peer-version-switch detection =====

TEST_F(QuicVersionNegotiatorTest, OnlyClientTreatsDifferingVersionAsPeerSwitch) {
    auto client = MakeNegotiator(/*is_server=*/false);
    auto server = MakeNegotiator(/*is_server=*/true);

    EXPECT_TRUE(client->IsPeerVersionSwitch(kQuicVersion1));
    EXPECT_FALSE(server->IsPeerVersionSwitch(kQuicVersion1)) << "the server drives its own upgrade from the peer TP";
}

TEST_F(QuicVersionNegotiatorTest, ZeroAndMatchingVersionsAreNotSwitches) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);

    EXPECT_FALSE(negotiator->IsPeerVersionSwitch(0)) << "0 means absent, not a change";
    EXPECT_FALSE(negotiator->IsPeerVersionSwitch(kQuicVersion2));
    EXPECT_FALSE(negotiator->DiffersFromCurrent(0));
    EXPECT_TRUE(negotiator->DiffersFromCurrent(kQuicVersion1));
}

// ===== original_version bookkeeping =====

// Only the peer's FIRST Initial counts: the RFC 9368 §4 check compares against
// what was originally on the wire, so a later packet must not overwrite it.
TEST_F(QuicVersionNegotiatorTest, OnlyFirstPeerVersionIsRecorded) {
    auto negotiator = MakeNegotiator(/*is_server=*/true);
    TransportParam remote;

    negotiator->RecordPeerOriginalVersion(kQuicVersion1);
    negotiator->RecordPeerOriginalVersion(kQuicVersion2);

    // Observable through the consistency check: chosen_version must match the
    // first recorded wire version, not the second.
    remote.SetVersionInformation(kQuicVersion1, {kQuicVersion1});
    EXPECT_TRUE(negotiator->ValidateAndMaybeUpgradeByRemoteTP(remote));
    EXPECT_EQ(close_count_, 0);
}

TEST_F(QuicVersionNegotiatorTest, ZeroPeerVersionIsIgnored) {
    auto negotiator = MakeNegotiator(/*is_server=*/true);
    TransportParam remote;

    negotiator->RecordPeerOriginalVersion(0);
    negotiator->RecordPeerOriginalVersion(kQuicVersion1);

    remote.SetVersionInformation(kQuicVersion1, {kQuicVersion1});
    EXPECT_TRUE(negotiator->ValidateAndMaybeUpgradeByRemoteTP(remote));
    EXPECT_EQ(close_count_, 0);
}

// ===== RFC 9368 §4 consistency check =====

TEST_F(QuicVersionNegotiatorTest, AbsentPeerVersionInformationIsTolerated) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    TransportParam remote;
    ASSERT_FALSE(remote.HasVersionInformation());

    EXPECT_TRUE(negotiator->ValidateAndMaybeUpgradeByRemoteTP(remote)) << "peers predating RFC 9368 must keep working";
    EXPECT_EQ(close_count_, 0);
}

TEST_F(QuicVersionNegotiatorTest, MismatchedPeerChosenVersionClosesConnection) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    TransportParam remote;
    // Client expects the server's chosen_version to equal the wire version it
    // has been decrypting with (v2 by default).
    remote.SetVersionInformation(kQuicVersion1, {kQuicVersion1});

    EXPECT_FALSE(negotiator->ValidateAndMaybeUpgradeByRemoteTP(remote));
    EXPECT_EQ(close_count_, 1);
    EXPECT_EQ(last_close_error_, static_cast<uint64_t>(QuicErrorCode::kVersionNegotiationError));
}

// RFC 9368 §4 downgrade detection: if we asked for v2 and the server says it
// supports v2 yet we ended up on v1, someone tampered with the exchange.
TEST_F(QuicVersionNegotiatorTest, ClientDetectsDowngradeWhenPeerAdvertisesOurPreference) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    negotiator->ApplyVersion(kQuicVersion1);
    negotiator->SetPreferredVersion(kQuicVersion2);

    TransportParam remote;
    remote.SetVersionInformation(kQuicVersion1, {kQuicVersion2, kQuicVersion1});

    EXPECT_FALSE(negotiator->ValidateAndMaybeUpgradeByRemoteTP(remote));
    EXPECT_EQ(close_count_, 1);
    EXPECT_EQ(last_close_error_, static_cast<uint64_t>(QuicErrorCode::kVersionNegotiationError));
}

// Without an explicit preference there is no "expected" outcome to compare
// against, so no downgrade can be inferred.
TEST_F(QuicVersionNegotiatorTest, NoPreferenceMeansNoDowngradeInference) {
    auto negotiator = MakeNegotiator(/*is_server=*/false);
    negotiator->ApplyVersion(kQuicVersion1);

    TransportParam remote;
    remote.SetVersionInformation(kQuicVersion1, {kQuicVersion2, kQuicVersion1});

    EXPECT_TRUE(negotiator->ValidateAndMaybeUpgradeByRemoteTP(remote));
    EXPECT_EQ(close_count_, 0);
    EXPECT_TRUE(negotiator->IsCompatVnCompleted());
}

// Server with no preference stays on whatever the client chose.
TEST_F(QuicVersionNegotiatorTest, ServerWithoutPreferenceStaysOnPeerChosenVersion) {
    auto negotiator = MakeNegotiator(/*is_server=*/true);
    TransportParam remote;
    remote.SetVersionInformation(kQuicVersion2, {kQuicVersion2, kQuicVersion1});

    EXPECT_TRUE(negotiator->ValidateAndMaybeUpgradeByRemoteTP(remote));
    EXPECT_EQ(negotiator->GetVersion(), kQuicVersion2);
    EXPECT_EQ(crypto_->GetVersion(), kQuicVersion2);
    EXPECT_TRUE(negotiator->IsCompatVnCompleted());
}

// The switch is idempotent: a second TP must not re-run it.
TEST_F(QuicVersionNegotiatorTest, ValidationIsIdempotentOnceCompleted) {
    auto negotiator = MakeNegotiator(/*is_server=*/true);
    TransportParam remote;
    remote.SetVersionInformation(kQuicVersion2, {kQuicVersion2});

    ASSERT_TRUE(negotiator->ValidateAndMaybeUpgradeByRemoteTP(remote));
    ASSERT_TRUE(negotiator->IsCompatVnCompleted());

    EXPECT_TRUE(negotiator->ValidateAndMaybeUpgradeByRemoteTP(remote));
    EXPECT_EQ(close_count_, 0);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
