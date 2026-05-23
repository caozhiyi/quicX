// =============================================================================
// profile_blocked_registry.cpp - Targeted CPU profile of
// QpackBlockedRegistry::AckByStreamId / RemoveByStreamId.
//
// Rationale:
//   BM_Qpack_BlockedRegistry_AddAck shows super-linear scaling:
//     N=16  -> 1.47 us   ( ~92 ns / op )
//     N=128 -> 20.1 us   ( ~157 ns / op )
//     N=1024 -> 1042 us  ( ~1017 ns / op )   <-- O(N^2) smoking gun
//
//   A full O(N^2) scan at N=1024 visits ~524k entries, which matches ~1 ms at
//   ~2 ns/iteration.  Source inspection suggests the culprit is
//   FindEarliestForStream() inside blocked_registry.cpp, which linearly scans
//   pending_ for every AckByStreamId/RemoveByStreamId call.  This driver
//   captures a flamegraph so we can confirm and quantify.
// =============================================================================
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "common/log/log.h"
#include "http3/qpack/blocked_registry.h"

#include "test/perf/tools/sampling_profiler.h"

using namespace quicx;

int main(int argc, char** argv) {
    int hz = 997;
    int seconds = 5;
    int n_pending = 1024;
    const char* out = "/tmp/blocked_registry_stacks.raw";
    bool mute_log = true;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hz") && i + 1 < argc) hz = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--n") && i + 1 < argc) n_pending = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--keep-log")) mute_log = false;
    }
    if (mute_log) {
        common::LOG_SET_LEVEL(common::LogLevel::kNull);
    }

    std::fprintf(stderr, "profile_blocked_registry: N=%d pending streams\n", n_pending);

    {
        std::string maps_path = std::string(out) + ".maps";
        FILE* mfp = std::fopen(maps_path.c_str(), "w");
        if (mfp) {
            FILE* self = std::fopen("/proc/self/maps", "r");
            if (self) {
                char buf[512];
                while (std::fgets(buf, sizeof(buf), self)) std::fputs(buf, mfp);
                std::fclose(self);
            }
            std::fclose(mfp);
        }
    }

    perf::SamplingProfiler prof(out, hz);
    prof.Start();

    auto t0 = std::chrono::steady_clock::now();
    uint64_t iters = 0;
    const auto deadline = t0 + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        http3::QpackBlockedRegistry reg;
        reg.SetMaxBlockedStreams(0);  // unlimited
        // Fill
        for (int i = 0; i < n_pending; ++i) {
            uint64_t key = (static_cast<uint64_t>(i) << 32);
            reg.Add(key, []() {});
        }
        // Drain via AckByStreamId / RemoveByStreamId — this is the O(N^2) path.
        for (int i = 0; i < n_pending; ++i) {
            uint64_t sid = static_cast<uint64_t>(i);
            if (i & 1) reg.RemoveByStreamId(sid);
            else       reg.AckByStreamId(sid);
        }
        ++iters;
    }
    prof.Stop();
    auto t1 = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    std::fprintf(stderr,
        "performed %lu fill+drain cycles (N=%d) in %.3f s  => %.1f us/cycle  (samples=%u)\n",
        (unsigned long)iters, n_pending, elapsed_s,
        elapsed_s * 1e6 / iters, prof.SampleCount());

    prof.Dump();
    return 0;
}
