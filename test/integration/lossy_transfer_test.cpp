// Lossy transfer integration test
// =================================
// Drives the full quicX client+server stack over loopback while injecting
// synthetic egress packet loss in UdpSender. Goal: cheaply reproduce the
// "server falls silent until idle timeout" symptom observed against the
// interop sim transfer-loss / transfer-corruption / mtu rounds, so root-cause
// diagnosis can be done with normal IDE / gdb / log tooling instead of the
// docker harness.
//
// Why this is the lowest-cost reproducer:
//   * Single process, no docker, no root.
//   * Reuses the LargeBodyDownload5MB pattern from streaming_and_push_test.
//   * Loss is symmetric across both endpoints (they share UdpSender), exactly
//     like the sim's bidirectional p2p-with-loss link.
//   * Per-test loss rate is tunable; we ramp from 0% (smoke baseline) up to
//     5% to provide a stress signal without making CI flake.
//
// IMPORTANT: this file deliberately includes the internal header
// "quic/udp/udp_sender.h". The integration CMake target adds
// ${CMAKE_SOURCE_DIR}/src to the include path for this binary only.

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include <quicx/http3/if_async_handler.h>
#include <quicx/http3/if_client.h>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <quicx/http3/if_server.h>

// Internal header: process-wide synthetic-loss switch on the egress path.
#include "quic/udp/udp_sender.h"

namespace {

constexpr size_t kOneMegabyte = 1u * 1024u * 1024u;
constexpr size_t kFiveMegabyte = 5u * 1024u * 1024u;

inline uint8_t LargeBodyByteAt(size_t index) {
    return static_cast<uint8_t>(index & 0xFF);
}

}  // namespace

#include "test_server_helper.h"

// ==================== Test Fixture ====================

class LossyTransferTest : public ::testing::Test {
protected:
    std::shared_ptr<quicx::IServer> server_;
    std::thread server_thread_;
    uint16_t port_;
    static std::atomic<uint16_t> next_port_;

    static const char cert_pem_[];
    static const char key_pem_[];

    // Aggregated fault-injection profile used by RunLossyDownload. Every
    // field defaults to "off", so an existing call site that passes only
    // a drop_per_million still gets the original semantics.
    struct FaultProfile {
        uint32_t drop_per_million = 0;   // 0 = no random loss
        uint64_t rate_limit_bps   = 0;   // 0 = unlimited
        uint32_t egress_delay_ms  = 0;   // 0 = immediate
    };

    void SetUp() override {
        // Probe for a kernel-confirmed free UDP port (see test_server_helper.h).
        port_ = quicx::test::ProbeFreeUdpPort(next_port_);
        ASSERT_NE(port_, 0u) << "failed to find a free UDP port for test server";
        // Always start with a clean slate even if a previous test in the same
        // binary aborted before TearDown.
        quicx::quic::UdpSender::ResetFaultInjection();
    }

    void TearDown() override {
        // Disable all faults BEFORE shutting down so connection close packets
        // get through reliably and the delay-queue worker has nothing to flush
        // into the next test.
        quicx::quic::UdpSender::ResetFaultInjection();

        if (server_) {
            server_->Stop();
            server_->Join();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        server_.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void StartServer() {
        quicx::Http3Settings settings = quicx::kDefaultHttp3Settings;
        server_ = quicx::IServer::Create(settings);

        quicx::Http3ServerConfig server_config;
        server_config.quic_config_.cert_pem_ = cert_pem_;
        server_config.quic_config_.key_pem_ = key_pem_;
        server_config.quic_config_.config_.worker_thread_num_ = 2;
        // Promote logging to kDebug so a stuck run leaves enough breadcrumbs
        // (flow-control / send-loop / timer churn) to diagnose. The output is
        // gated by the build's logger; run with QUICX_LOG_TO_STDOUT=1 (or
        // whatever the project uses) when debugging interactively.
        server_config.quic_config_.config_.log_level_ = quicx::LogLevel::kDebug;

        ASSERT_TRUE(server_->Init(server_config));
    }

    std::shared_ptr<quicx::IClient> CreateClient() {
        quicx::Http3Settings settings = quicx::kDefaultHttp3Settings;
        auto client = quicx::IClient::Create(settings);

        quicx::Http3ClientConfig config;
        config.quic_config_.verify_peer_ = false;
        config.quic_config_.config_.worker_thread_num_ = 2;
        config.quic_config_.config_.log_level_ = quicx::LogLevel::kDebug;
        // Bump connect timeout so the lossy handshake itself doesn't fail us
        // before we even reach the data-phase symptom we're hunting.
        config.connection_timeout_ms_ = 15000;

        if (!client->Init(config)) {
            return nullptr;
        }
        return client;
    }

    void StartServerThread() {
        // Synchronously bind on the main thread (Start() returns only once
        // the UDP socket is fully armed in the master event loop). This
        // surfaces bind() failures immediately as a fixture ASSERT.
        ASSERT_TRUE(server_->Start("127.0.0.1", port_));
    }

    // Streaming download handler that verifies the deterministic byte pattern
    // incrementally. Identical to StreamingAndPushTest's helper so failures
    // are directly comparable.
    class StreamingDownloadHandler : public quicx::IAsyncClientHandler {
    public:
        void OnHeaders(std::shared_ptr<quicx::IResponse> response) override {
            status_code = response->GetStatusCode();
            headers_received = true;
        }
        void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
            if (data && length > 0) {
                size_t base = received.load();
                for (size_t i = 0; i < length; ++i) {
                    if (data[i] != LargeBodyByteAt(base + i)) {
                        pattern_ok = false;
                        break;
                    }
                }
                received += length;
            }
            if (is_last) {
                completed = true;
            }
        }
        void OnError(uint32_t error_code) override {
            error = error_code;
            completed = true;
        }
        std::atomic<bool> headers_received{false};
        std::atomic<bool> completed{false};
        std::atomic<bool> pattern_ok{true};
        std::atomic<size_t> received{0};
        int status_code = 0;
        uint32_t error = 0;
    };

    // Run a GET that streams `total_size` bytes from the server back to the
    // client, with the fault profile `fp` applied for the whole transfer.
    // Returns the handler so the caller can assert details.
    std::shared_ptr<StreamingDownloadHandler> RunLossyDownload(
        size_t total_size,
        const FaultProfile& fp,
        std::chrono::seconds budget) {
        StartServer();

        const size_t total_bytes = total_size;
        server_->AddHandler(quicx::HttpMethod::kGet, "/download-large",
            [total_bytes](std::shared_ptr<quicx::IRequest> req,
                          std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                auto sent = std::make_shared<size_t>(0);
                resp->AddHeader("content-length", std::to_string(total_bytes));
                resp->SetResponseBodyProvider([sent, total_bytes](uint8_t* buf,
                                                                  size_t buf_size) -> size_t {
                    if (*sent >= total_bytes) {
                        return 0;
                    }
                    size_t remaining = total_bytes - *sent;
                    size_t to_write = std::min(remaining, buf_size);
                    for (size_t i = 0; i < to_write; ++i) {
                        buf[i] = LargeBodyByteAt(*sent + i);
                    }
                    *sent += to_write;
                    return to_write;
                });
            });

        StartServerThread();

        auto client = CreateClient();
        if (!client) {
            ADD_FAILURE() << "client init failed";
            return nullptr;
        }

        auto handler = std::make_shared<StreamingDownloadHandler>();
        auto request = quicx::IRequest::Create();
        std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/download-large";

        // Engage faults right before issuing the request. Doing it here
        // (rather than at fixture SetUp time) lets the QUIC handshake
        // complete cleanly so the test isolates the *data-phase* symptom.
        // Note: handshake packets between Init() above and DoRequest() below
        // are still subject to faults, which is what we want to mirror sim.
        quicx::quic::UdpSender::SetDropPerMillion(fp.drop_per_million);
        quicx::quic::UdpSender::SetRateLimitBps(fp.rate_limit_bps);
        quicx::quic::UdpSender::SetEgressDelayMs(fp.egress_delay_ms);

        client->DoRequest(url, quicx::HttpMethod::kGet, request, handler);

        const auto poll_step = std::chrono::milliseconds(50);
        const auto deadline = std::chrono::steady_clock::now() + budget;
        size_t last_received = 0;
        auto last_progress_ts = std::chrono::steady_clock::now();
        while (!handler->completed.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(poll_step);
            size_t r = handler->received.load();
            if (r != last_received) {
                last_received = r;
                last_progress_ts = std::chrono::steady_clock::now();
            } else {
                // Surface a "stuck" symptom early in test logs without
                // failing the test outright: ~3s without any new bytes
                // received is the bug fingerprint we're hunting.
                auto stuck_for = std::chrono::steady_clock::now() - last_progress_ts;
                if (stuck_for > std::chrono::seconds(3)) {
                    ::testing::Test::RecordProperty(
                        "stuck_bytes", std::to_string(last_received));
                }
            }
        }

        // Lift faults before Close() so close frames make it out reliably.
        quicx::quic::UdpSender::ResetFaultInjection();

        client->Close();
        return handler;
    }

    // Backwards-compat overload: existing tests just pass a drop rate.
    std::shared_ptr<StreamingDownloadHandler> RunLossyDownload(
        size_t total_size,
        uint32_t drop_per_million,
        std::chrono::seconds budget) {
        FaultProfile fp;
        fp.drop_per_million = drop_per_million;
        return RunLossyDownload(total_size, fp, budget);
    }
};

std::atomic<uint16_t> LossyTransferTest::next_port_(18900);

const char LossyTransferTest::cert_pem_[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICWDCCAcGgAwIBAgIJAPuwTC6rEJsMMA0GCSqGSIb3DQEBBQUAMEUxCzAJBgNV\n"
    "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
    "aWRnaXRzIFB0eSBMdGQwHhcNMTQwNDIzMjA1MDQwWhcNMTcwNDIyMjA1MDQwWjBF\n"
    "MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"
    "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKB\n"
    "gQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92kWdGMdAQhLci\n"
    "HnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiFKKAnHmUcrgfV\n"
    "W28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQABo1AwTjAdBgNV\n"
    "HQ4EFgQUi3XVrMsIvg4fZbf6Vr5sp3Xaha8wHwYDVR0jBBgwFoAUi3XVrMsIvg4f\n"
    "Zbf6Vr5sp3Xaha8wDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUFAAOBgQA76Hht\n"
    "ldY9avcTGSwbwoiuIqv0jTL1fHFnzy3RHMLDh+Lpvolc5DSrSJHCP5WuK0eeJXhr\n"
    "T5oQpHL9z/cCDLAKCKRa4uV0fhEdOWBqyR9p8y5jJtye72t6CuFUV5iqcpF4BH4f\n"
    "j2VNHwsSrJwkD4QUGlUtH7vwnQmyCFxZMmWAJg==\n"
    "-----END CERTIFICATE-----\n";

const char LossyTransferTest::key_pem_[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIICXgIBAAKBgQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92\n"
    "kWdGMdAQhLciHnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiF\n"
    "KKAnHmUcrgfVW28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQAB\n"
    "AoGBAIBy09Fd4DOq/Ijp8HeKuCMKTHqTW1xGHshLQ6jwVV2vWZIn9aIgmDsvkjCe\n"
    "i6ssZvnbjVcwzSoByhjN8ZCf/i15HECWDFFh6gt0P5z0MnChwzZmvatV/FXCT0j+\n"
    "WmGNB/gkehKjGXLLcjTb6dRYVJSCZhVuOLLcbWIV10gggJQBAkEA8S8sGe4ezyyZ\n"
    "m4e9r95g6s43kPqtj5rewTsUxt+2n4eVodD+ZUlCULWVNAFLkYRTBCASlSrm9Xhj\n"
    "QpmWAHJUkQJBAOVzQdFUaewLtdOJoPCtpYoY1zd22eae8TQEmpGOR11L6kbxLQsk\n"
    "aMly/DOnOaa82tqAGTdqDEZgSNmCeKKknmECQAvpnY8GUOVAubGR6c+W90iBuQLj\n"
    "LtFp/9ihd2w/PoDwrHZaoUYVcT4VSfJQog/k7kjE4MYXYWL8eEKg3WTWQNECQQDk\n"
    "104Wi91Umd1PzF0ijd2jXOERJU1wEKe6XLkYYNHWQAe5l4J4MWj9OdxFXAxIuuR/\n"
    "tfDwbqkta4xcux67//khAkEAvvRXLHTaa6VFzTaiiO8SaFsHV3lQyXOtMrBpB5jd\n"
    "moZWgjHvB2W9Ckn7sDqsPB+U2tyX0joDdQEyuiMECDY8oQ==\n"
    "-----END RSA PRIVATE KEY-----\n";

// ==================== Sanity test (zero loss) ====================

// Baseline: with the loss switch installed but disabled, behaviour must match
// LargeBodyDownload5MB exactly. This guards against the fault-injection patch
// regressing the happy path.
TEST_F(LossyTransferTest, BaselineNoLoss5MB) {
    auto handler = RunLossyDownload(kFiveMegabyte, /*drop_pm=*/0,
                                     std::chrono::seconds(60));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load());
    EXPECT_EQ(handler->status_code, 200);
    EXPECT_EQ(handler->error, 0u);
    EXPECT_EQ(handler->received.load(), kFiveMegabyte);
    EXPECT_TRUE(handler->pattern_ok.load());
}

// ==================== Repro tests ====================

// 1% loss on a 1 MiB transfer. With a healthy stack this finishes well under
// 30 s on loopback. EXPECTED to pass: this is the "should still work" floor.
TEST_F(LossyTransferTest, OnePercentLoss1MB) {
    auto handler = RunLossyDownload(kOneMegabyte, /*drop_pm=*/10'000,
                                     std::chrono::seconds(30));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load())
        << "1MB stuck under 1% loss; received="
        << handler->received.load();
    EXPECT_EQ(handler->error, 0u);
    EXPECT_EQ(handler->received.load(), kOneMegabyte);
}

// 1% loss on a 5 MiB transfer. This is the closest analogue to the interop
// sim transfer-loss case. With the bug present we expect the transfer to
// stall part-way through and the test to fail by timeout.
TEST_F(LossyTransferTest, OnePercentLoss5MB) {
    auto handler = RunLossyDownload(kFiveMegabyte, /*drop_pm=*/10'000,
                                     std::chrono::seconds(45));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load())
        << "5MB stuck under 1% loss; received="
        << handler->received.load();
    EXPECT_EQ(handler->error, 0u);
    EXPECT_EQ(handler->received.load(), kFiveMegabyte);
}

// 5% loss on 5 MiB. Stress level used to differentiate "occasional stall" from
// "consistent stall". If only this case fails, the bug is loss-density
// dependent; if both 1% and 5% fail, it's a deterministic state-machine bug.
TEST_F(LossyTransferTest, FivePercentLoss5MB) {
    auto handler = RunLossyDownload(kFiveMegabyte, /*drop_pm=*/50'000,
                                     std::chrono::seconds(60));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load())
        << "5MB stuck under 5% loss; received="
        << handler->received.load();
    EXPECT_EQ(handler->error, 0u);
    EXPECT_EQ(handler->received.load(), kFiveMegabyte);
}

// 10% loss on 5 MiB. Loss density much higher than the interop sim, used to
// flush out any rare-window state-machine bug that depends on consecutive
// drops (e.g. an entire flight + the first retransmit both lost). Marked
// DISABLED because it can flake on CI; run explicitly with
//   --gtest_also_run_disabled_tests --gtest_filter='*HighLoss*'
TEST_F(LossyTransferTest, DISABLED_TenPercentLoss5MB) {
    auto handler = RunLossyDownload(kFiveMegabyte, /*drop_pm=*/100'000,
                                     std::chrono::seconds(120));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load())
        << "5MB stuck under 10% loss; received="
        << handler->received.load();
    EXPECT_EQ(handler->received.load(), kFiveMegabyte);
}

// 20% loss. Extreme stress. Useful when manually hunting for stalls; not part
// of the default suite because it's deliberately brutal.
TEST_F(LossyTransferTest, DISABLED_TwentyPercentLoss5MB) {
    auto handler = RunLossyDownload(kFiveMegabyte, /*drop_pm=*/200'000,
                                     std::chrono::seconds(180));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load())
        << "5MB stuck under 20% loss; received="
        << handler->received.load();
    EXPECT_EQ(handler->received.load(), kFiveMegabyte);
}

// ==================== Sim-mirror tests (rate + delay + loss) ====================
//
// The bandwidth/delay knobs were added because pure loopback loss-only tests
// (above) cannot reproduce the interop sim "transfer-loss" / "mtu" stalls:
// loopback bandwidth is effectively infinite and RTT is microseconds, so the
// stack never sits long enough at the cwnd / pacing / flow-control boundary
// to expose the bug. The sim runs with ~1 Mbps bandwidth, 5 ms one-way delay
// (~10 ms RTT), and a small AQM queue. The tests here aim to mimic that
// environment within a single process by combining UdpSender's three knobs.
//
// Helpful reference numbers when reading these tests:
//   1 Mbps  =       125'000  bytes/s     (sim default)
//   2 Mbps  =       250'000  bytes/s
//   5 MiB at 1 Mbps                      ~= 42 s ideal
//   5 MiB at 2 Mbps                      ~= 21 s ideal
//
// All these tests are DISABLED by default because each runs for tens of
// seconds and is meant to be invoked manually (or by a dedicated CI lane)
// when hunting stalls:
//   ./bin/lossy_transfer_test --gtest_also_run_disabled_tests \
//       --gtest_filter='*SimMirror*'

// 1 Mbps bottleneck + 5 ms delay, NO synthetic loss. If this stalls, the bug
// is purely in the bandwidth / pacing / flow-control feedback loop and has
// nothing to do with packet recovery. Useful as a control test.
TEST_F(LossyTransferTest, DISABLED_SimMirror1MbpsNoLoss5MB) {
    FaultProfile fp;
    fp.rate_limit_bps  = 125'000;   // 1 Mbps
    fp.egress_delay_ms = 5;          // 10 ms RTT
    auto handler = RunLossyDownload(kFiveMegabyte, fp,
                                     std::chrono::seconds(120));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load())
        << "5MB stuck under sim-mirror (1Mbps + 5ms, no loss); received="
        << handler->received.load();
    EXPECT_EQ(handler->error, 0u);
    EXPECT_EQ(handler->received.load(), kFiveMegabyte);
}

// Closest single-process analogue to the interop "transfer-loss" round:
// 1 Mbps bottleneck + 5 ms one-way delay + 1% iid loss. The combined
// pressure is the most likely setting to surface the "server falls silent
// until idle timeout" symptom locally. Generous budget (180s) because at
// 1 Mbps, 5 MiB is ~42s ideal; with loss + recovery it is realistically
// 50-90s when healthy.
TEST_F(LossyTransferTest, DISABLED_SimMirror1Mbps1pctLoss5MB) {
    FaultProfile fp;
    fp.drop_per_million = 10'000;    // 1% loss
    fp.rate_limit_bps   = 125'000;   // 1 Mbps
    fp.egress_delay_ms  = 5;          // 10 ms RTT
    auto handler = RunLossyDownload(kFiveMegabyte, fp,
                                     std::chrono::seconds(180));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load())
        << "5MB stuck under sim-mirror (1Mbps + 5ms + 1% loss); received="
        << handler->received.load();
    EXPECT_EQ(handler->received.load(), kFiveMegabyte);
}

// Smaller body variant for faster turnaround when iterating on a fix.
// Same fault profile as the 5MB sim-mirror case, but only 1 MiB so each
// debug iteration takes ~10s instead of ~60s.
TEST_F(LossyTransferTest, DISABLED_SimMirror1Mbps1pctLoss1MB) {
    FaultProfile fp;
    fp.drop_per_million = 10'000;
    fp.rate_limit_bps   = 125'000;
    fp.egress_delay_ms  = 5;
    auto handler = RunLossyDownload(kOneMegabyte, fp,
                                     std::chrono::seconds(60));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load())
        << "1MB stuck under sim-mirror (1Mbps + 5ms + 1% loss); received="
        << handler->received.load();
    EXPECT_EQ(handler->received.load(), kOneMegabyte);
}

// 2 Mbps + 5 ms + 1% loss. Same regime, more headroom -- if this passes
// while the 1 Mbps version hangs, the bug is gated on the bottleneck being
// fully saturated (pacing / cwnd-limited path), as opposed to a generic
// loss-recovery problem.
TEST_F(LossyTransferTest, DISABLED_SimMirror2Mbps1pctLoss5MB) {
    FaultProfile fp;
    fp.drop_per_million = 10'000;
    fp.rate_limit_bps   = 250'000;   // 2 Mbps
    fp.egress_delay_ms  = 5;
    auto handler = RunLossyDownload(kFiveMegabyte, fp,
                                     std::chrono::seconds(120));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->completed.load())
        << "5MB stuck under sim-mirror (2Mbps + 5ms + 1% loss); received="
        << handler->received.load();
    EXPECT_EQ(handler->received.load(), kFiveMegabyte);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
