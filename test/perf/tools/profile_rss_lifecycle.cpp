// =============================================================================
// profile_rss_lifecycle.cpp
//
// Distinguish "real per-connection growth" vs "glibc heap fragmentation /
// one-shot bookkeeping" for the QUIC/HTTP3 client stack.
//
// The e2e_perf_test Stability_ConnectDisconnect benchmark reports
// rss_per_conn = (rss_end - rss_start) / N after N back-to-back
// connect->request->close cycles.  That number is:
//   * influenced by one-shot allocations (malloc arenas, thread stacks,
//     epoll buffers, static caches populated the first time around);
//   * influenced by glibc's retention policy (arenas aren't always shrunk
//     even when all application objects are freed);
//   * *not* the live working set of N concurrently open connections.
//
// This driver separates those concerns:
//
//   1. Warmup the process (arenas, TLS caches, epoll fds, worker thread).
//   2. For each batch size N in {0, 1, 2, 5, 10, 20, 50, 100}:
//        (a) snapshot RSS + mallinfo2 *before* the batch
//        (b) run N connect->request->close cycles
//        (c) snapshot RSS + mallinfo2 *after* the batch, pre-trim
//        (d) call malloc_trim(0) and snapshot again (post-trim)
//   3. Fit `rss_after_trim(N) = A + B * N` and report both A and B.
//      B is the *real* per-connection residue in glibc's books.
//
// A diagnostic tool, not a google-benchmark target.  Built via add_profiler()
// in test/perf/CMakeLists.txt.
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <malloc.h>
#include <unistd.h>
#endif

#include <quicx/http3/if_client.h>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <quicx/http3/if_server.h>
#include "quic/connection/controler/rtt_calculator.h"
#include <quicx/common/metrics.h>

using quicx::IClient;
using quicx::IRequest;
using quicx::IResponse;
using quicx::IServer;
using quicx::Http3Settings;
using quicx::Http3ClientConfig;
using quicx::Http3ServerConfig;
using quicx::HttpMethod;
using quicx::LogLevel;
using quicx::kDefaultHttp3Settings;

namespace {

// -------- Same test certs as e2e_perf_test.cpp / http3_e2e_bench ------------
const char kCert[] =
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

const char kKey[] =
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

static Http3Settings MakeSettings() {
    // Match the benchmark's exact defaults so per-conn numbers correspond.
    return kDefaultHttp3Settings;
}

// -------------------------------------------------------------------------
// RSS + mallinfo2 snapshots
// -------------------------------------------------------------------------
struct MemSample {
    size_t rss_bytes = 0;
    size_t heap_in_use = 0;     // mallinfo2.uordblks  - bytes currently allocated
    size_t heap_total = 0;      // mallinfo2.arena + hblkhd - bytes in heap arenas
    size_t heap_fordblks = 0;   // free blocks in arena
};

static size_t ReadRssBytes() {
#if defined(__linux__)
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long pages = 0;
    if (std::fscanf(f, "%*ld %ld", &pages) != 1) pages = 0;
    std::fclose(f);
    return static_cast<size_t>(pages) * static_cast<size_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;
#endif
}

static MemSample Sample() {
    MemSample m;
    m.rss_bytes = ReadRssBytes();
#if defined(__linux__) && defined(__GLIBC__)
    struct mallinfo2 mi = mallinfo2();
    m.heap_in_use = mi.uordblks;
    m.heap_total = mi.arena + mi.hblkhd;
    m.heap_fordblks = mi.fordblks;
#endif
    return m;
}

static void PrintHeader() {
    std::printf("%-25s %12s %12s %12s %12s\n",
                "phase", "rss_MiB", "heap_use_MiB", "heap_tot_MiB", "heap_free_MiB");
    std::printf("%-25s %12s %12s %12s %12s\n",
                "-----", "-------", "------------", "------------", "-------------");
}

static void PrintSample(const char* tag, const MemSample& m) {
    std::printf("%-25s %12.3f %12.3f %12.3f %12.3f\n",
                tag,
                m.rss_bytes / (1024.0 * 1024.0),
                m.heap_in_use / (1024.0 * 1024.0),
                m.heap_total / (1024.0 * 1024.0),
                m.heap_fordblks / (1024.0 * 1024.0));
}

struct BatchResult {
    int n;
    MemSample before;
    MemSample after_raw;
    MemSample after_trim;
    int64_t active_conns_before = -1;
    int64_t active_conns_after = -1;
    int64_t streams_active_before = -1;
    int64_t streams_active_after = -1;
};

// Extract an int metric value from a Prometheus text dump, matching the
// first line that starts with `name ` or `name{`.
static int64_t ParsePromMetric(const std::string& dump, const std::string& name) {
    size_t pos = 0;
    while (pos < dump.size()) {
        size_t nl = dump.find('\n', pos);
        std::string line = dump.substr(pos, (nl == std::string::npos ? dump.size() : nl) - pos);
        pos = (nl == std::string::npos) ? dump.size() : nl + 1;
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind(name, 0) != 0) continue;
        char c = (line.size() > name.size()) ? line[name.size()] : '\0';
        if (c != ' ' && c != '{') continue;
        size_t sp = line.rfind(' ');
        if (sp == std::string::npos) continue;
        try { return std::stoll(line.substr(sp + 1)); } catch (...) { return -1; }
    }
    return -1;
}

static BatchResult RunBatch(int n, const std::string& url, const Http3Settings& settings) {
    BatchResult r;
    r.n = n;

    // Quiesce then sample
#if defined(__linux__) && defined(__GLIBC__)
    malloc_trim(0);
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    r.before = Sample();
    {
        std::string dump = quicx::common::Metrics::ExportPrometheus();
        r.active_conns_before = ParsePromMetric(dump, "quic_connections_active");
        r.streams_active_before = ParsePromMetric(dump, "quic_streams_active");
    }

    int success = 0;
    for (int i = 0; i < n; ++i) {
        auto client = IClient::Create(settings);
        Http3ClientConfig cc;
        cc.quic_config_.verify_peer_ = false;
        cc.quic_config_.config_.worker_thread_num_ = 1;
        cc.quic_config_.config_.log_level_ = LogLevel::kError;
        client->Init(cc);

        auto req = IRequest::Create();
        std::atomic<bool> done{false};
        std::atomic<bool> ok{false};
        client->DoRequest(url, HttpMethod::kGet, req,
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

    // Give any dangling tasks a chance to finish their teardown callbacks.
    // 2s is enough to cover:
    //   * the CONNECTION_CLOSE path's draining delay
    //   * the Client::Close() safety-net 1000ms fallback Destroy timer
    //   * any AddTimer(0, ...) reschedules the event loop processes lazily
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    r.after_raw = Sample();

#if defined(__linux__) && defined(__GLIBC__)
    malloc_trim(0);
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    r.after_trim = Sample();
    {
        std::string dump = quicx::common::Metrics::ExportPrometheus();
        r.active_conns_after = ParsePromMetric(dump, "quic_connections_active");
        r.streams_active_after = ParsePromMetric(dump, "quic_streams_active");
    }

    if (success < n) {
        std::fprintf(stderr, "  [warn] batch n=%d success=%d/%d\n", n, success, n);
    }
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    int port = 19599;
    std::vector<int> batches = {1, 2, 5, 10, 20, 50};
    bool include_100 = false;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--port") && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--with-100")) {
            include_100 = true;
        }
    }
    if (include_100) batches.push_back(100);

    // Use the same benchmark-side RTT override we apply in e2e_perf_test.
    quicx::quic::SetDefaultInitialRtt(100);

    auto settings = MakeSettings();
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

    std::string host = "127.0.0.1";
    std::string url = "https://" + host + ":" + std::to_string(port) + "/cd";
    std::thread server_thread([&]() { server->Start(host, port); });
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // Warmup: hit the server once so first-time caches / TLS session resumption
    // slots / stream pools are hot before we start sampling.
    int warm_ok = 0;
    for (int w = 0; w < 3; ++w) {
        auto client = IClient::Create(settings);
        Http3ClientConfig cc;
        cc.quic_config_.verify_peer_ = false;
        cc.quic_config_.config_.worker_thread_num_ = 1;
        cc.quic_config_.config_.log_level_ = LogLevel::kWarn;
        client->Init(cc);
        auto req = IRequest::Create();
        std::atomic<bool> done{false};
        std::atomic<bool> ok{false};
        std::atomic<uint32_t> err{0};
        client->DoRequest(url, HttpMethod::kGet, req,
            [&](std::shared_ptr<IResponse>, uint32_t e) { if (e == 0) ok = true; err = e; done = true; });
        for (int t = 0; t < 2000 && !done; ++t) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (ok) warm_ok++;
        std::fprintf(stderr, "  warmup attempt %d: done=%d ok=%d err=%u\n",
                     w, (int)done, (int)ok, (unsigned)err.load());
        client->Close();
    }
    std::printf("warmup         : %d/3 succeeded\n", warm_ok);
    if (warm_ok == 0) {
        std::fprintf(stderr, "ERROR: warmup did not succeed; aborting.\n");
        server->Stop();
        server_thread.join();
        return 2;
    }

    std::printf("\n===== profile_rss_lifecycle =====\n");
    std::printf("url            : %s\n", url.c_str());
    std::printf("batches        : ");
    for (int n : batches) std::printf("%d ", n);
    std::printf("\n\n");

    PrintHeader();

    std::vector<BatchResult> results;
    results.reserve(batches.size());
    for (int n : batches) {
        auto r = RunBatch(n, url, settings);
        results.push_back(r);
        std::printf("\n[batch n=%d]\n", n);
        PrintSample("before",      r.before);
        PrintSample("after (raw)", r.after_raw);
        PrintSample("after (trim)",r.after_trim);
        std::printf("  quic_connections_active: before=%lld after=%lld (delta=%lld)\n",
                    (long long)r.active_conns_before,
                    (long long)r.active_conns_after,
                    (long long)(r.active_conns_after - r.active_conns_before));
        std::printf("  quic_streams_active    : before=%lld after=%lld (delta=%lld)\n",
                    (long long)r.streams_active_before,
                    (long long)r.streams_active_after,
                    (long long)(r.streams_active_after - r.streams_active_before));
    }

    std::printf("\n===== per-connection deltas (after trim, RSS-based) =====\n");
    std::printf("%-8s %16s %16s %16s %16s\n",
                "N",
                "dRSS_KB",
                "dRSS/N_KB",
                "dHeapUse_KB",
                "dHeapUse/N_KB");
    for (const auto& r : results) {
        double drss     = (double)((long long)r.after_trim.rss_bytes - (long long)r.before.rss_bytes) / 1024.0;
        double dheap    = (double)((long long)r.after_trim.heap_in_use - (long long)r.before.heap_in_use) / 1024.0;
        std::printf("%-8d %16.1f %16.2f %16.1f %16.2f\n",
                    r.n, drss, drss / r.n, dheap, dheap / r.n);
    }

    // Linear fit on RSS: rss_delta(N) = A + B*N  using least squares.
    if (results.size() >= 2) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int k = 0;
        for (const auto& r : results) {
            double x = r.n;
            double y = (double)((long long)r.after_trim.rss_bytes - (long long)r.before.rss_bytes);
            sx += x; sy += y; sxx += x * x; sxy += x * y; ++k;
        }
        double denom = k * sxx - sx * sx;
        if (denom != 0) {
            double slope = (k * sxy - sx * sy) / denom;        // bytes per conn
            double intercept = (sy - slope * sx) / k;           // bytes one-shot
            std::printf("\nleast-squares fit (RSS, post-trim): dRSS = %.0f + %.0f * N  (bytes)\n",
                        intercept, slope);
            std::printf("  => one-shot overhead A ≈ %.1f KB\n", intercept / 1024.0);
            std::printf("  => per-connection residue B ≈ %.2f KB\n", slope / 1024.0);
        }

        // Same fit on heap-in-use (ignores RSS quantisation + arena retention).
        sx = sy = sxx = sxy = 0; k = 0;
        for (const auto& r : results) {
            double x = r.n;
            double y = (double)((long long)r.after_trim.heap_in_use - (long long)r.before.heap_in_use);
            sx += x; sy += y; sxx += x * x; sxy += x * y; ++k;
        }
        denom = k * sxx - sx * sx;
        if (denom != 0) {
            double slope = (k * sxy - sx * sy) / denom;
            double intercept = (sy - slope * sx) / k;
            std::printf("\nleast-squares fit (heap_in_use, post-trim): dHeap = %.0f + %.0f * N  (bytes)\n",
                        intercept, slope);
            std::printf("  => one-shot overhead A ≈ %.1f KB\n", intercept / 1024.0);
            std::printf("  => per-connection residue B ≈ %.2f KB\n", slope / 1024.0);
        }
    }

    std::printf("\nInterpretation\n");
    std::printf("  * If heap_in_use slope ≈ 0, the \"per-connection residue\" we see in\n");
    std::printf("    RSS is glibc arena retention, not an application leak.\n");
    std::printf("  * If heap_in_use slope is non-trivial, every connect-disconnect\n");
    std::printf("    cycle leaves application-owned memory behind.  That is a leak\n");
    std::printf("    to hunt down.\n");
    std::printf("  * If RSS slope >> heap_in_use slope, malloc_trim isn't keeping up\n");
    std::printf("    and we should consider MALLOC_ARENA_MAX / periodic trim.\n\n");

    server->Stop();
    server_thread.join();
    return 0;
}
