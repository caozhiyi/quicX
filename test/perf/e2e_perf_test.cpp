// =============================================================================
// e2e_perf_test.cpp - End-to-End Performance Benchmarks for quicX
// =============================================================================
//
// Full-chain performance tests covering the 4 standard analysis scenarios:
//   Scenario 1: Single-connection handshake (TLS/crypto hotspot identification)
//   Scenario 2: High-throughput data transfer (data processing hotspot)
//   Scenario 3: High-concurrency small requests (stream management hotspot)
//   Scenario 4: Long-running stability (memory leak / resource growth)
//
// These tests exercise the entire HTTP/3 + QUIC stack from client to server,
// including TLS handshake, QPACK header compression, frame encoding/decoding,
// congestion control, flow control, and connection management.
//
// Build:
//   cmake -B build -DENABLE_PERF_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
//   cmake --build build --target e2e_perf_test -j
//
// Usage:
//   # Run all E2E benchmarks
//   ./build/bin/perf/e2e_perf_test
//
//   # Run specific scenario
//   ./build/bin/perf/e2e_perf_test --benchmark_filter="Handshake"
//   ./build/bin/perf/e2e_perf_test --benchmark_filter="Throughput"
//   ./build/bin/perf/e2e_perf_test --benchmark_filter="Concurrency"
//   ./build/bin/perf/e2e_perf_test --benchmark_filter="Stability"
//
//   # Generate flame graph (full chain analysis)
//   ./scripts/perf/generate_flamegraph.sh -d 60 ./build/bin/perf/e2e_perf_test
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

// Platform-specific includes for RSS tracking
#if defined(__APPLE__)
#include <mach/mach.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

// HTTP/3 client/server API
#include <quicx/http3/if_client.h>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <quicx/http3/if_server.h>

// P3 knob: lower the process-wide initial RTT for loopback benchmarks.
// Production code never touches this header; we reach into the internal
// rtt_calculator here because benchmarks are explicitly a "test-side opt-in"
// consumer of the default-override hook described in
// docs/internal/perf_e2e_analysis.md §6 P3.
#include "quic/connection/controler/rtt_calculator.h"

namespace quicx {
namespace perf {

// ===========================================================================
// Test certificates (same as integration tests & http3_e2e_bench)
// ===========================================================================

static const char kCert[] =
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

static const char kKey[] =
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

// ===========================================================================
// Cold-start warmup
// ===========================================================================
//
// Every benchmark shares one unavoidable tax: the *first* datagram a fresh
// client sends to a fresh server has a non-trivial probability of being lost
// inside the stack (queue/schedule race) — and the client's initial PTO is
// 775 ms (see docs/internal/perf_e2e_analysis.md §3 for the math). When that firing
// lands inside the measured loop, the resulting ±1 s swing can dominate
// benchmarks whose real steady-state cost is a few milliseconds.
//
// WarmupServer() pays that tax *once*, before the benchmark timer starts,
// by performing a single disposable request against the server. After it
// returns, the server's event loop has been pumped, the OS socket state is
// warm, and subsequent connections will observe clean first-sample RTTs.
//
// This is not "cheating" — it just shifts the cold-start penalty out of the
// measurement window. Real-world QUIC clients pool connections and therefore
// never see this initial state in hot paths.
static void WarmupServer(const std::string& url, const Http3Settings& settings) {
    auto warm = IClient::Create(settings);
    Http3ClientConfig wcc;
    wcc.quic_config_.verify_peer_ = false;
    wcc.quic_config_.config_.worker_thread_num_ = 1;
    wcc.quic_config_.config_.log_level_ = LogLevel::kError;
    warm->Init(wcc);

    auto req = IRequest::Create();
    std::atomic<bool> done{false};
    warm->DoRequest(url, HttpMethod::kGet, req,
        [&](std::shared_ptr<IResponse>, uint32_t) { done = true; });
    for (int w = 0; w < 2000 && !done; ++w) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    warm->Close();
    // Give the Close()'s AddTimer(0) Destroy chain a moment to actually run
    // on the warmup client's event loop before we drop the unique_ptr. With
    // the P1 fixes this typically completes in <50 ms on localhost.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// ===========================================================================
// RSS (Resident Set Size) tracking
// ===========================================================================

static size_t GetCurrentRSS() {
#if defined(__APPLE__)
    struct task_basic_info info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#elif defined(__linux__)
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long pages = 0;
    if (fscanf(f, "%*ld %ld", &pages) != 1) pages = 0;
    fclose(f);
    return pages * sysconf(_SC_PAGESIZE);
#else
    return 0;
#endif
}

// ===========================================================================
// Scenario 1: Single-Connection Handshake
// ===========================================================================
//
// Goal: Identify TLS/crypto hotspots in the full handshake path.
// Each iteration: new client -> QUIC handshake -> HTTP/3 request -> teardown.
// Exercises: TLS, Initial/Handshake packets, QPACK init, stream creation.
//

static void BM_E2E_Handshake_NewConnection(benchmark::State& state) {
    // Setup server (identical pattern to proven http3_e2e_bench)
    Http3Settings settings = kDefaultHttp3Settings;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kGet, "/ping",
        [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody("pong");
        });

    Http3ServerConfig sc;
    sc.quic_config_.cert_pem_ = kCert;
    sc.quic_config_.key_pem_ = kKey;
    sc.quic_config_.config_.worker_thread_num_ = 1;
    sc.quic_config_.config_.log_level_ = LogLevel::kWarn;
    server->Init(sc);

    std::thread th([&]() { server->Start("127.0.0.1", 19501); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WarmupServer("https://127.0.0.1:19501/ping", settings);

    int64_t success = 0;

    for (auto _ : state) {
        // Each iteration: create a new client = new QUIC connection
        auto client = IClient::Create(settings);
        Http3ClientConfig cc;
        cc.quic_config_.verify_peer_ = false;
        cc.quic_config_.config_.worker_thread_num_ = 1;
        cc.quic_config_.config_.log_level_ = LogLevel::kWarn;
        client->Init(cc);

        auto req = IRequest::Create();
        std::atomic<bool> done{false};
        std::atomic<bool> ok{false};

        bool sent = client->DoRequest("https://127.0.0.1:19501/ping", HttpMethod::kGet, req,
            [&](std::shared_ptr<IResponse> resp, uint32_t error) {
                if (error == 0) ok = true;
                done = true;
            });
        if (!sent) {
            fprintf(stderr, "[DBG] DoRequest returned false\n");
        }

        for (int w = 0; w < 2000 && !done; ++w) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (ok) success++;
        client->Close();
    }

    server->Stop();
    th.join();

    state.counters["success"] = benchmark::Counter(static_cast<double>(success));
    state.counters["handshakes/s"] = benchmark::Counter(
        static_cast<double>(success), benchmark::Counter::kIsRate);
}

// Burst: many new connections concurrently (reflects real "connection storm" traffic).
//
// Rationale: QUIC's design intent is "1 connection, many requests". A *sequential*
// create/close loop cannot reflect handshake throughput because `IClient::Close()`
// is asynchronous (schedules Destroy() after kConnectionCloseDestroyTimeoutMs=1s),
// so back-to-back clients pile up zombie sockets/workers and exhaust the single
// server worker thread (causing 10s timeouts after ~5 iterations).
//
// This version spawns `burst_size` threads that each perform one full handshake+request
// in parallel, then all clients are closed only AFTER all have finished. This measures
// server-side handshake throughput under concurrent new-connection pressure.
static void BM_E2E_Handshake_Burst(benchmark::State& state) {
    Http3Settings settings = kDefaultHttp3Settings;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kGet, "/ping",
        [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody("pong");
        });

    Http3ServerConfig sc;
    sc.quic_config_.cert_pem_ = kCert;
    sc.quic_config_.key_pem_ = kKey;
    // NOTE: kMultiThread mode triggers a "double free" in the current quicX build;
    // keep single-thread server here. This makes the benchmark effectively measure
    // server single-worker handshake capacity under a concurrent client storm — the
    // very bottleneck this scenario is designed to expose.
    sc.quic_config_.config_.thread_mode_ = ThreadMode::kSingleThread;
    sc.quic_config_.config_.worker_thread_num_ = 1;
    sc.quic_config_.config_.log_level_ = LogLevel::kError;
    server->Init(sc);

    const int burst_size = static_cast<int>(state.range(0));
    const uint16_t port = 19502;

    std::thread th([&]() { server->Start("127.0.0.1", port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const std::string url = "https://127.0.0.1:" + std::to_string(port) + "/ping";
    WarmupServer(url, settings);

    for (auto _ : state) {
        std::atomic<int> success{0};
        std::vector<std::thread> workers;
        std::vector<std::unique_ptr<IClient>> clients(burst_size);
        std::vector<std::atomic<bool>> ready_flags(burst_size);
        for (auto& f : ready_flags) f.store(false);
        workers.reserve(burst_size);

        std::atomic<bool> go{false};

        for (int i = 0; i < burst_size; ++i) {
            workers.emplace_back([&, i]() {
                auto client = IClient::Create(settings);
                Http3ClientConfig cc;
                cc.quic_config_.verify_peer_ = false;
                cc.quic_config_.config_.worker_thread_num_ = 1;
                cc.quic_config_.config_.log_level_ = LogLevel::kError;
                client->Init(cc);

                // Publish the client pointer to the outer vector (so Close() can be
                // called from the main thread after everyone joins). We raw-pointer
                // it to avoid shared_ptr and keep sole ownership in the vector.
                IClient* raw = client.release();
                clients[i].reset(raw);

                ready_flags[i].store(true, std::memory_order_release);
                while (!go.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                auto req = IRequest::Create();
                std::atomic<bool> done{false};
                std::atomic<bool> ok{false};
                raw->DoRequest(url, HttpMethod::kGet, req,
                    [&](std::shared_ptr<IResponse>, uint32_t error) {
                        if (error == 0) ok = true;
                        done = true;
                    });
                for (int w = 0; w < 6000 && !done; ++w) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                if (ok) success.fetch_add(1, std::memory_order_relaxed);
            });
        }

        // Wait until every worker has initialized its client, then release them together.
        bool all_ready = false;
        while (!all_ready) {
            all_ready = true;
            for (auto& f : ready_flags) {
                if (!f.load(std::memory_order_acquire)) { all_ready = false; break; }
            }
            if (!all_ready) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        go.store(true, std::memory_order_release);

        for (auto& w : workers) w.join();

        // Now tear down all clients. With the P1 fixes in place, Close() on a
        // client with live connections:
        //   1. invokes the application-level close callback synchronously from
        //      inside OnStateToClosing (so we already know all peers observed
        //      CONNECTION_CLOSE by the time ~Client() runs), and
        //   2. schedules Destroy() via AddTimer(0, ...) — i.e. asynchronously
        //      on the quic client's own event loop, but without the historical
        //      1 s safety-timer tax.
        // A short settle window is still required so the AddTimer(0) chain has
        // a chance to run (one event-loop tick per client) before we launch
        // the next iteration's storm on the same server port.
        for (auto& c : clients) {
            if (c) c->Close();
        }
        clients.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        state.counters["success_rate_%"] = benchmark::Counter(
            static_cast<double>(success.load()) / burst_size * 100.0);
    }

    server->Stop();
    th.join();

    state.SetItemsProcessed(state.iterations() * burst_size);
}

// ===========================================================================
// Scenario 2: High-Throughput Data Transfer
// ===========================================================================
//
// Goal: Identify data processing hotspots in send/receive path.
// Persistent client transfers data over a single long-lived connection.
// Exercises: buffer management, flow control, congestion control,
//            encryption/decryption, ACK processing, stream frames.
//

// Download: server -> client, 1MB per request
static void BM_E2E_Throughput_Download(benchmark::State& state) {
    const std::string big_body(1024 * 1024, 'D');  // 1MB

    Http3Settings settings = kDefaultHttp3Settings;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kGet, "/download",
        [&big_body](std::shared_ptr<IRequest>, std::shared_ptr<IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody(big_body);
        });

    Http3ServerConfig sc;
    sc.quic_config_.cert_pem_ = kCert;
    sc.quic_config_.key_pem_ = kKey;
    sc.quic_config_.config_.worker_thread_num_ = 1;
    sc.quic_config_.config_.log_level_ = LogLevel::kError;
    server->Init(sc);

    std::thread th([&]() { server->Start("127.0.0.1", 19503); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WarmupServer("https://127.0.0.1:19503/download", settings);

    auto client = IClient::Create(settings);
    Http3ClientConfig cc;
    cc.quic_config_.verify_peer_ = false;
    cc.quic_config_.config_.worker_thread_num_ = 1;
    cc.quic_config_.config_.log_level_ = LogLevel::kError;
    client->Init(cc);

    int64_t total_bytes = 0;

    for (auto _ : state) {
        auto req = IRequest::Create();
        std::atomic<bool> done{false};
        std::string body;
        client->DoRequest("https://127.0.0.1:19503/download", HttpMethod::kGet, req,
            [&](std::shared_ptr<IResponse> resp, uint32_t error) {
                if (error == 0 && resp) body = resp->GetBodyAsString();
                done = true;
            });
        for (int w = 0; w < 10000 && !done; ++w) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        total_bytes += static_cast<int64_t>(body.size());
    }

    client->Close();
    server->Stop();
    th.join();

    state.SetBytesProcessed(total_bytes);
    state.counters["avg_body_KB"] = benchmark::Counter(
        state.iterations() > 0 ? static_cast<double>(total_bytes) / state.iterations() / 1024.0 : 0);
}

// Upload: client -> server, configurable size
static void BM_E2E_Throughput_Upload(benchmark::State& state) {
    const size_t upload_size = static_cast<size_t>(state.range(0));
    const std::string upload_data(upload_size, 'U');

    Http3Settings settings = kDefaultHttp3Settings;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kPost, "/upload",
        [](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody(std::to_string(req->GetBodyAsString().size()));
        });

    Http3ServerConfig sc;
    sc.quic_config_.cert_pem_ = kCert;
    sc.quic_config_.key_pem_ = kKey;
    sc.quic_config_.config_.worker_thread_num_ = 1;
    sc.quic_config_.config_.log_level_ = LogLevel::kError;
    server->Init(sc);

    std::thread th([&]() { server->Start("127.0.0.1", 19504); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WarmupServer("https://127.0.0.1:19504/upload", settings);

    auto client = IClient::Create(settings);
    Http3ClientConfig cc2;
    cc2.quic_config_.verify_peer_ = false;
    cc2.quic_config_.config_.worker_thread_num_ = 1;
    cc2.quic_config_.config_.log_level_ = LogLevel::kError;
    client->Init(cc2);

    int64_t total_bytes = 0;

    for (auto _ : state) {
        auto req = IRequest::Create();
        req->AppendBody(upload_data);
        std::atomic<bool> done{false};
        std::atomic<bool> ok{false};
        client->DoRequest("https://127.0.0.1:19504/upload", HttpMethod::kPost, req,
            [&](std::shared_ptr<IResponse>, uint32_t error) {
                if (error == 0) ok = true;
                done = true;
            });
        for (int w = 0; w < 10000 && !done; ++w) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (ok) total_bytes += static_cast<int64_t>(upload_size);
    }

    client->Close();
    server->Stop();
    th.join();

    state.SetBytesProcessed(total_bytes);
    state.counters["upload_MB/s"] = benchmark::Counter(
        static_cast<double>(total_bytes) / 1024.0 / 1024.0,
        benchmark::Counter::kIsRate);
}

// Sequential: many request/response round-trips over a single connection
static void BM_E2E_Throughput_Sequential(benchmark::State& state) {
    Http3Settings settings = kDefaultHttp3Settings;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kGet, "/echo",
        [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody(std::string(1024, 'E'));
        });

    Http3ServerConfig sc;
    sc.quic_config_.cert_pem_ = kCert;
    sc.quic_config_.key_pem_ = kKey;
    sc.quic_config_.config_.worker_thread_num_ = 1;
    sc.quic_config_.config_.log_level_ = LogLevel::kError;
    server->Init(sc);

    std::thread th([&]() { server->Start("127.0.0.1", 19505); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WarmupServer("https://127.0.0.1:19505/echo", settings);

    auto client = IClient::Create(settings);
    Http3ClientConfig cc2;
    cc2.quic_config_.verify_peer_ = false;
    cc2.quic_config_.config_.worker_thread_num_ = 1;
    cc2.quic_config_.config_.log_level_ = LogLevel::kError;
    client->Init(cc2);

    const int reqs = static_cast<int>(state.range(0));
    int64_t total = 0;

    for (auto _ : state) {
        int success = 0;
        for (int i = 0; i < reqs; ++i) {
            auto req = IRequest::Create();
            std::atomic<bool> done{false};
            std::atomic<bool> ok{false};
            client->DoRequest("https://127.0.0.1:19505/echo", HttpMethod::kGet, req,
                [&](std::shared_ptr<IResponse>, uint32_t error) {
                    if (error == 0) ok = true;
                    done = true;
                });
            for (int w = 0; w < 2000 && !done; ++w) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (ok) success++;
        }
        total += success;
    }

    client->Close();
    server->Stop();
    th.join();

    state.SetItemsProcessed(total);
    state.counters["req/s"] = benchmark::Counter(
        static_cast<double>(total), benchmark::Counter::kIsRate);
}

// ===========================================================================
// Scenario 3: High-Concurrency Small Requests
// ===========================================================================
//
// Goal: Identify stream management and scheduling hotspots.
// Exercises: stream ID allocation, concurrent multiplexing, QPACK under load,
//            flow control arbitration, worker scheduling.
//

// Multiple concurrent streams from a single client (fire-and-forget + wait)
static void BM_E2E_Concurrency_MultiStream(benchmark::State& state) {
    Http3Settings settings = kDefaultHttp3Settings;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kGet, "/small",
        [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody(std::string(1024, 'S'));
        });

    Http3ServerConfig sc;
    sc.quic_config_.cert_pem_ = kCert;
    sc.quic_config_.key_pem_ = kKey;
    sc.quic_config_.config_.worker_thread_num_ = 1;
    sc.quic_config_.config_.log_level_ = LogLevel::kError;
    server->Init(sc);

    std::thread th([&]() { server->Start("127.0.0.1", 19506); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WarmupServer("https://127.0.0.1:19506/small", settings);

    auto client = IClient::Create(settings);
    Http3ClientConfig cc2;
    cc2.quic_config_.verify_peer_ = false;
    cc2.quic_config_.config_.worker_thread_num_ = 1;
    cc2.quic_config_.config_.log_level_ = LogLevel::kError;
    client->Init(cc2);

    const int concurrent = static_cast<int>(state.range(0));

    for (auto _ : state) {
        std::atomic<int> completed{0};
        std::atomic<int> success{0};

        // Fire all requests concurrently
        for (int i = 0; i < concurrent; ++i) {
            auto req = IRequest::Create();
            client->DoRequest("https://127.0.0.1:19506/small", HttpMethod::kGet, req,
                [&](std::shared_ptr<IResponse>, uint32_t error) {
                    if (error == 0) {
                        success++;
                    }
                    completed++;
                });
        }

        // Wait for all
        for (int w = 0; w < 10000 && completed < concurrent; ++w) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        state.counters["success_rate_%"] = benchmark::Counter(
            concurrent > 0 ? static_cast<double>(success.load()) / concurrent * 100.0 : 0);
    }

    client->Close();
    server->Stop();
    th.join();

    state.SetItemsProcessed(state.iterations() * concurrent);
}

// Multiple client connections in parallel
static void BM_E2E_Concurrency_MultiClient(benchmark::State& state) {
    Http3Settings settings = kDefaultHttp3Settings;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kGet, "/mc",
        [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody(std::string(1024, 'M'));
        });

    Http3ServerConfig sc;
    sc.quic_config_.cert_pem_ = kCert;
    sc.quic_config_.key_pem_ = kKey;
    sc.quic_config_.config_.worker_thread_num_ = 1;
    sc.quic_config_.config_.log_level_ = LogLevel::kError;
    server->Init(sc);

    std::thread th([&]() { server->Start("127.0.0.1", 19507); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WarmupServer("https://127.0.0.1:19507/mc", settings);

    const int num_clients = static_cast<int>(state.range(0));
    const int reqs_per = 5;

    for (auto _ : state) {
        std::atomic<int64_t> total_success{0};
        std::vector<std::thread> threads;

        for (int i = 0; i < num_clients; ++i) {
            threads.emplace_back([&]() {
                auto client = IClient::Create(settings);
                Http3ClientConfig cc2;
                cc2.quic_config_.verify_peer_ = false;
                cc2.quic_config_.config_.worker_thread_num_ = 1;
                cc2.quic_config_.config_.log_level_ = LogLevel::kError;
                client->Init(cc2);

                for (int j = 0; j < reqs_per; ++j) {
                    auto req = IRequest::Create();
                    std::atomic<bool> done{false};
                    client->DoRequest("https://127.0.0.1:19507/mc", HttpMethod::kGet, req,
                        [&](std::shared_ptr<IResponse>, uint32_t error) {
                            if (error == 0) total_success++;
                            done = true;
                        });
                    for (int w = 0; w < 2000 && !done; ++w) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }
                client->Close();
            });
        }

        for (auto& t : threads) t.join();

        state.counters["success_rate_%"] = benchmark::Counter(
            static_cast<double>(total_success.load()) / (num_clients * reqs_per) * 100.0);
    }

    server->Stop();
    th.join();

    state.SetItemsProcessed(state.iterations() * num_clients * reqs_per);
}

// ===========================================================================
// Scenario 4: Long-Running Stability
// ===========================================================================
//
// Goal: Detect memory leaks, resource growth, and degradation over time.
// Tracks: RSS growth, throughput, success rate.
//

// Sustained load for N seconds
static void BM_E2E_Stability_SustainedLoad(benchmark::State& state) {
    Http3Settings settings = kDefaultHttp3Settings;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kGet, "/stable",
        [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody("stable");
        });

    Http3ServerConfig sc;
    sc.quic_config_.cert_pem_ = kCert;
    sc.quic_config_.key_pem_ = kKey;
    sc.quic_config_.config_.worker_thread_num_ = 1;
    sc.quic_config_.config_.log_level_ = LogLevel::kError;
    server->Init(sc);

    std::thread th([&]() { server->Start("127.0.0.1", 19508); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WarmupServer("https://127.0.0.1:19508/stable", settings);

    const int duration_sec = static_cast<int>(state.range(0));

    for (auto _ : state) {
        size_t rss_start = GetCurrentRSS();
        auto t0 = std::chrono::steady_clock::now();
        std::atomic<int64_t> ok{0}, fail{0};
        std::atomic<bool> stop{false};

        // Single client, continuous requests
        auto client = IClient::Create(settings);
        Http3ClientConfig cc2;
        cc2.quic_config_.verify_peer_ = false;
        cc2.quic_config_.config_.worker_thread_num_ = 1;
        cc2.quic_config_.config_.log_level_ = LogLevel::kError;
        client->Init(cc2);

        std::thread worker([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                auto req = IRequest::Create();
                std::atomic<bool> done{false};
                std::atomic<bool> success{false};
                client->DoRequest("https://127.0.0.1:19508/stable", HttpMethod::kGet, req,
                    [&](std::shared_ptr<IResponse>, uint32_t error) {
                        if (error == 0) success = true;
                        done = true;
                    });
                for (int w = 0; w < 1000 && !done && !stop.load(std::memory_order_relaxed); ++w) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                if (success) ok++;
                else fail++;
            }
        });

        std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
        stop = true;
        worker.join();
        client->Close();

        auto t1 = std::chrono::steady_clock::now();
        size_t rss_end = GetCurrentRSS();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        state.counters["success"] = benchmark::Counter(static_cast<double>(ok.load()));
        state.counters["fail"] = benchmark::Counter(static_cast<double>(fail.load()));
        state.counters["avg_req/s"] = benchmark::Counter(
            elapsed > 0 ? static_cast<double>(ok.load()) / elapsed : 0);
        if (rss_start > 0 && rss_end > 0) {
            state.counters["rss_delta_KB"] = benchmark::Counter(
                static_cast<double>(static_cast<int64_t>(rss_end) - static_cast<int64_t>(rss_start)) / 1024.0);
        }
    }

    server->Stop();
    th.join();
}

// Repeated connect/disconnect cycles — detect connection-related leaks
static void BM_E2E_Stability_ConnectDisconnect(benchmark::State& state) {
    Http3Settings settings = kDefaultHttp3Settings;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kGet, "/cd",
        [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody("ok");
        });

    Http3ServerConfig sc;
    sc.quic_config_.cert_pem_ = kCert;
    sc.quic_config_.key_pem_ = kKey;
    sc.quic_config_.config_.worker_thread_num_ = 1;
    sc.quic_config_.config_.log_level_ = LogLevel::kError;
    server->Init(sc);

    std::thread th([&]() { server->Start("127.0.0.1", 19509); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WarmupServer("https://127.0.0.1:19509/cd", settings);

    const int cycles = static_cast<int>(state.range(0));

    for (auto _ : state) {
        size_t rss_start = GetCurrentRSS();
        int success = 0;

        for (int i = 0; i < cycles; ++i) {
            auto client = IClient::Create(settings);
            Http3ClientConfig cc2;
            cc2.quic_config_.verify_peer_ = false;
            cc2.quic_config_.config_.worker_thread_num_ = 1;
            cc2.quic_config_.config_.log_level_ = LogLevel::kError;
            client->Init(cc2);

            auto req = IRequest::Create();
            std::atomic<bool> done{false};
            std::atomic<bool> ok{false};
            client->DoRequest("https://127.0.0.1:19509/cd", HttpMethod::kGet, req,
                [&](std::shared_ptr<IResponse>, uint32_t error) {
                    if (error == 0) ok = true;
                    done = true;
                });
            for (int w = 0; w < 2000 && !done; ++w) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (ok) success++;
            client->Close();
        }

        size_t rss_end = GetCurrentRSS();
        state.counters["success"] = benchmark::Counter(static_cast<double>(success));
        if (rss_start > 0 && rss_end > 0) {
            int64_t delta = static_cast<int64_t>(rss_end) - static_cast<int64_t>(rss_start);
            state.counters["rss_delta_KB"] = benchmark::Counter(
                static_cast<double>(delta) / 1024.0);
            state.counters["rss_per_conn_B"] = benchmark::Counter(
                cycles > 0 ? static_cast<double>(delta) / cycles : 0);
        }
    }

    server->Stop();
    th.join();

    state.SetItemsProcessed(state.iterations() * cycles);
}

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Register Benchmarks
// ===========================================================================

// --- Scenario 1: Handshake ---
//
// Iteration counts: google-benchmark rejects combining Iterations() with
// MinTime() (they are mutually exclusive). We pin Iterations() to a floor
// that is large enough to average out cold-start jitter (~1 s PTO hit) into
// the noise: at Iterations(5) a single outlier contributes 20% of the
// reported mean, at Iterations(10) only 10%. This is the P2 "harden the
// harness" fix from docs/internal/perf_e2e_analysis.md §6. The previous
// Iterations(2) made a single cold-start miss swing the reported time by
// ±50%, which is the main cause of the 50×–400× run-to-run variance noted
// in §4.
BENCHMARK(quicx::perf::BM_E2E_Handshake_NewConnection)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(10)
    ->UseRealTime();

BENCHMARK(quicx::perf::BM_E2E_Handshake_Burst)
    ->Arg(5)->Arg(10)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(5)
    ->UseRealTime();

// --- Scenario 2: Throughput ---
BENCHMARK(quicx::perf::BM_E2E_Throughput_Download)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(10)
    ->UseRealTime();

BENCHMARK(quicx::perf::BM_E2E_Throughput_Upload)
    ->Arg(1024)          // 1KB
    ->Arg(64 * 1024)     // 64KB
    ->Arg(256 * 1024)    // 256KB
    ->Unit(benchmark::kMillisecond)
    ->Iterations(10)
    ->UseRealTime();

BENCHMARK(quicx::perf::BM_E2E_Throughput_Sequential)
    ->Arg(10)->Arg(50)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(5)
    ->UseRealTime();

// --- Scenario 3: Concurrency ---
BENCHMARK(quicx::perf::BM_E2E_Concurrency_MultiStream)
    ->Arg(5)->Arg(10)->Arg(20)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(5)
    ->UseRealTime();

BENCHMARK(quicx::perf::BM_E2E_Concurrency_MultiClient)
    ->Arg(2)->Arg(5)->Arg(10)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(5)
    ->UseRealTime();

// --- Scenario 4: Stability ---
// Stability tests have long built-in runtimes (SustainedLoad uses state.range(0)
// as the *duration* in seconds, ConnectDisconnect loops cycles internally).
// Keep Iterations(1) here — running them twice would just double wall-clock
// cost without adding signal.
BENCHMARK(quicx::perf::BM_E2E_Stability_SustainedLoad)
    ->Arg(5)->Arg(10)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1)
    ->UseRealTime();

BENCHMARK(quicx::perf::BM_E2E_Stability_ConnectDisconnect)
    ->Arg(10)->Arg(20)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1)
    ->UseRealTime();

// ----------------------------------------------------------------------------
// Custom main: replace BENCHMARK_MAIN() so we can install the P3 initial-RTT
// override *before* any benchmark (and therefore any RttCalculator) is
// constructed. The hook reduces the pre-handshake PTO budget from 775 ms to
// ~100 ms on loopback, which is what collapses the residual run-to-run
// variance in Handshake_NewConnection / ConnectDisconnect tests (where each
// iteration creates a fresh client and would otherwise occasionally pay the
// full 775 ms first-datagram PTO).
//
// This is an *opt-in* used only by this benchmark binary. The shipped library
// continues to use the RFC-friendly default (kInitRttDefaultMs = 250 ms).
int main(int argc, char** argv) {
    // 100 ms chosen empirically:
    //   - large enough to absorb normal loopback socket scheduling (<1 ms)
    //     plus event-loop wakeups (~5 ms typical, up to ~30 ms under
    //     contention) plus all the usual debug-build overhead;
    //   - small enough that a spurious first-Initial drop costs ~200 ms of
    //     PTO instead of ~1 s (base PTO = 100 + 200 + 0 = 300 ms on a cold
    //     connection, then exponential backoff);
    //   - conservative enough not to collapse the small inter-packet windows
    //     that legitimate close/teardown logic relies on to drain unacked
    //     packet timers in order. Lowering this to 50 ms was observed to
    //     occasionally race connection teardown on heavy MultiStream runs.
    ::quicx::quic::SetDefaultInitialRtt(100);

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}

#else
int main() {
    return 0;
}
#endif
