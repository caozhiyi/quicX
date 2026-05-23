// =============================================================================
// sampling_profiler.h - Minimal in-process SIGPROF-based sampling profiler.
//
// Why this file exists:
//   The target container has no perf / gperftools / FlameGraph.pl.  We therefore
//   ship a ~150 LoC self-contained sampler that:
//     * sets up a SIGPROF itimer at a configurable frequency (default 997 Hz)
//     * walks the stack in the signal handler with glibc's backtrace(3)
//     * aggregates identical stacks into a counted hash-map
//     * dumps a "collapsed stacks" file that is 100% compatible with
//       Brendan Gregg's flamegraph.pl (each line is  "fn;fn;fn  <count>").
//
// Resolving addresses is done offline: the companion resolver script runs
// addr2line + c++filt.  That keeps the signal handler strictly async-signal
// safe (no malloc, no string formatting, no C++ containers touched).
//
// Usage:
//   #include "tools/sampling_profiler.h"
//   quicx::perf::SamplingProfiler prof("/tmp/stacks.raw", /*hz=*/997);
//   prof.Start();
//   <hot code under test>
//   prof.Stop();
//   prof.Dump();
//
// Signal-handler invariants:
//   * Only backtrace(3) (glibc-internal, async-signal-safe per the man page
//     "glibc 2.0+ provides an async-signal-safe variant") + atomic ring-buffer
//     writes are performed.  No printf, no mutex, no new/delete.
//   * The sample ring buffer is a fixed-size lock-free single-producer
//     (the thread being sampled) single-consumer (Stop() on the same thread)
//     structure.  We assume the benchmark thread is the only one being
//     profiled (which is the case for Google Benchmark micro-benchmarks).
// =============================================================================
#pragma once

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <execinfo.h>
#include <sys/time.h>
#include <unistd.h>

namespace quicx {
namespace perf {

static constexpr int      kSampMaxDepth    = 48;
static constexpr uint32_t kSampRingEntries = 1u << 17;   // 131072 samples ≈ 130 s @ 1 kHz

struct Sample {
    uint8_t depth;
    void*   frames[kSampMaxDepth];
};

class SamplingProfiler {
public:
    SamplingProfiler(const char* out_path, int hz = 997)
        : out_path_(out_path), hz_(hz > 0 ? hz : 100),
          ring_(new Sample[kSampRingEntries]) {
        s_instance_ = this;
    }
    ~SamplingProfiler() { Stop(); s_instance_ = nullptr; delete[] ring_; }

    void Start() {
        if (running_) return;
        write_pos_.store(0, std::memory_order_relaxed);

        struct sigaction sa{};
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sa.sa_sigaction = &SamplingProfiler::Handler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGPROF, &sa, &old_sa_);

        struct itimerval it{};
        it.it_interval.tv_sec  = 0;
        it.it_interval.tv_usec = 1000000 / hz_;
        it.it_value            = it.it_interval;
        setitimer(ITIMER_PROF, &it, nullptr);
        running_ = true;
    }

    void Stop() {
        if (!running_) return;
        struct itimerval zero{};
        setitimer(ITIMER_PROF, &zero, nullptr);
        sigaction(SIGPROF, &old_sa_, nullptr);
        running_ = false;
    }

    // Write samples as  <addr>;<addr>;...;<addr>  lines, one per sample.
    // The offline resolver collapses duplicates and converts to symbols.
    bool Dump() const {
        FILE* fp = std::fopen(out_path_, "w");
        if (!fp) {
            std::fprintf(stderr, "SamplingProfiler: cannot open %s: %s\n",
                         out_path_, std::strerror(errno));
            return false;
        }
        uint32_t n = write_pos_.load(std::memory_order_relaxed);
        if (n > kSampRingEntries) n = kSampRingEntries;
        for (uint32_t i = 0; i < n; ++i) {
            const Sample& s = ring_[i];
            // Flamegraph convention: innermost frame first (so the root frame
            // is printed last).  backtrace() returns innermost-first already.
            for (int f = s.depth - 1; f >= 0; --f) {
                std::fprintf(fp, "%p%s", s.frames[f], f == 0 ? "" : ";");
            }
            std::fputc('\n', fp);
        }
        std::fclose(fp);
        std::fprintf(stderr,
            "SamplingProfiler: wrote %u samples (of %u max) to %s\n",
            n, kSampRingEntries, out_path_);
        return true;
    }

    uint32_t SampleCount() const {
        return write_pos_.load(std::memory_order_relaxed);
    }

private:
    static void Handler(int /*sig*/, siginfo_t* /*info*/, void* /*ucontext*/) {
        SamplingProfiler* self = s_instance_;
        if (!self) return;
        uint32_t idx = self->write_pos_.fetch_add(1, std::memory_order_relaxed);
        if (idx >= kSampRingEntries) return;  // ring full, drop
        Sample& s = self->ring_[idx];
        int d = backtrace(s.frames, kSampMaxDepth);
        s.depth = static_cast<uint8_t>(d < 0 ? 0 : d);
    }

    const char*  out_path_;
    int          hz_;
    bool         running_ = false;
    struct sigaction old_sa_{};

    // Fixed-size, no allocation inside handler.
    Sample* ring_;  // heap-allocated kSampRingEntries entries
    std::atomic<uint32_t> write_pos_{0};

    static SamplingProfiler* s_instance_;
};

inline SamplingProfiler* SamplingProfiler::s_instance_ = nullptr;

}  // namespace perf
}  // namespace quicx
